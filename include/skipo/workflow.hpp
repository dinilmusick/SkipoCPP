#pragma once
#include <skipo/application.hpp>
#include <atomic>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace skipo {
using Values=std::map<std::string,kloks::Value>;
struct WorkflowStep {
  std::string id,service,operation;
  std::vector<std::string> depends_on;
  std::function<kloks::Value(const Values&,const Values&)> make_input;
  unsigned max_attempts{1};
  bool retry_safe{false};
};
struct Trace {
  std::string correlation_id, step, service, operation;
  unsigned attempt{0};
  bool success{false};
  std::string detail;
};
enum class ExecutionState { completed, failed, cancelled };
struct Failure { ErrorCode code; std::string step, message; };
struct Execution {
  ExecutionState state{ExecutionState::failed};
  Values output;
  std::vector<Trace> traces;
  std::optional<Failure> failure;
  std::string correlation_id;
  bool from_idempotency_cache{false};
};
struct ExecutionOptions {
  std::string correlation_id, idempotency_key;
  const std::atomic_bool* cancel{nullptr};
};
// Caller-driven DAG. No background worker, shared scheduler or implicit persistence.
// No thread-safe concurrent run on the same Workflow; cancellation may be set by a separate thread.
class Workflow {
  std::map<std::string,WorkflowStep> steps_;
  std::vector<std::string> ordered_;
  bool dirty_{true};
  struct CacheEntry {Values input;Execution result;};
  std::map<std::string,CacheEntry> cache_;
  static std::vector<std::string> sort(const std::map<std::string,WorkflowStep>& steps) {
    std::map<std::string,int> colors;
    std::vector<std::string> sorted;
    std::function<void(const std::string&)> visit=[&](const std::string& id){
      if(colors[id]==2)return;
      if(colors[id]==1)throw Error(ErrorCode::cycle,id,"workflow dependency cycle");
      colors[id]=1;
      const auto& step=steps.at(id);
      for(const auto& dep:step.depends_on) {
        if(!steps.count(dep))throw Error(ErrorCode::invalid_definition,id,"missing upstream step: "+dep);
        visit(dep);
      }
      colors[id]=2;sorted.push_back(id);
    };
    for(const auto& entry:steps)visit(entry.first);
    return sorted;
  }
 public:
  void add(WorkflowStep step) {
    if(step.id.empty()||step.service.empty()||step.operation.empty()||step.max_attempts==0 || (step.max_attempts>1&&!step.retry_safe))
      throw Error(ErrorCode::invalid_definition,step.id,"invalid workflow step or unsafe retry");
    if(steps_.count(step.id))throw Error(ErrorCode::duplicate,step.id,"duplicate workflow step");
    std::set<std::string> unique;
    for(const auto& dep:step.depends_on) {
      if(dep==step.id||dep.empty())
        throw Error(ErrorCode::invalid_definition,step.id,"invalid dependency");
      if(!unique.insert(dep).second)
        throw Error(ErrorCode::duplicate,step.id,"duplicate dependency");
    }
    steps_.emplace(step.id,std::move(step));dirty_=true;cache_.clear();
  }
  void validate() {
    if(steps_.empty())throw Error(ErrorCode::invalid_definition,"workflow","empty workflow");
    ordered_=sort(steps_);dirty_=false;
  }
 private:
  template<class Dispatch>
  Execution execute(Dispatch&& dispatch,const Values& input,ExecutionOptions options) {
    if(dirty_) validate();
    Execution out;
    out.correlation_id=options.correlation_id;
    const auto cancelled=[&]{return options.cancel && options.cancel->load();};
    if(cancelled()) {
      out.state=ExecutionState::cancelled;
      out.failure=Failure{ErrorCode::cancelled,ordered_.front(),"execution cancelled"};
      return out;
    }
    for(const auto& [id,step]:steps_) {
      (void)id;
      if(step.max_attempts>1 && options.idempotency_key.empty())
        throw Error(ErrorCode::invalid_definition,step.id,"retry requires an idempotency key");
    }
    if(!options.idempotency_key.empty()) {
      auto it=cache_.find(options.idempotency_key);
      if(it!=cache_.end()) {
        if(it->second.input!=input)
          throw Error(ErrorCode::idempotency_conflict,"workflow","idempotency key reused for different input");
        auto result=it->second.result;
        result.from_idempotency_cache=true;
        return result;
      }
    }
    for(const auto& id:ordered_) {
      if(cancelled()) {
        out.state=ExecutionState::cancelled;
        out.failure=Failure{ErrorCode::cancelled,id,"execution cancelled"};
        return out;
      }
      const auto& step=steps_.at(id);
      kloks::Value argument;
      try {
        if(step.make_input) {
          Values predecessors;
          for(const auto& dependency:step.depends_on)
            predecessors.emplace(dependency,out.output.at(dependency));
          argument=step.make_input(predecessors,input);
        } else {
          const auto it=input.find(id);
          if(it!=input.end()) argument=it->second;
        }
      } catch(const std::exception& error) {
        out.failure=Failure{ErrorCode::invalid_definition,id,error.what()};
        return out;
      } catch(...) {
        out.failure=Failure{ErrorCode::invalid_definition,id,"unknown input mapping failure"};
        return out;
      }
      for(unsigned attempt=1;attempt<=step.max_attempts;++attempt) {
        if(cancelled()) {
          out.state=ExecutionState::cancelled;
          out.failure=Failure{ErrorCode::cancelled,id,"execution cancelled"};
          return out;
        }
        Trace trace{out.correlation_id,id,step.service,step.operation,attempt,false,{}};
        try {
          out.output[id]=dispatch(step.service,step.operation,argument);
          trace.success=true;
          out.traces.push_back(std::move(trace));
          break;
        } catch(const kloks::ServiceError& error) {
          trace.detail=error.what();
          out.traces.push_back(std::move(trace));
          if(attempt==step.max_attempts || error.code()!=kloks::ErrorCode::host_failure) {
            out.failure=Failure{ErrorCode::operation_failed,id,error.what()};
            return out;
          }
        } catch(const Error& error) {
          trace.detail=error.what();
          out.traces.push_back(std::move(trace));
          if(attempt==step.max_attempts || error.code()!=ErrorCode::host_failed) {
            out.failure=Failure{error.code(),id,error.what()};
            return out;
          }
        } catch(const std::exception& error) {
          trace.detail=error.what();
          out.traces.push_back(std::move(trace));
          if(attempt==step.max_attempts) {
            out.failure=Failure{ErrorCode::operation_failed,id,error.what()};
            return out;
          }
        } catch(...) {
          trace.detail="unknown operation failure";
          out.traces.push_back(std::move(trace));
          out.failure=Failure{ErrorCode::operation_failed,id,"unknown operation failure"};
          return out;
        }
      }
    }
    out.state=ExecutionState::completed;
    if(!options.idempotency_key.empty()) cache_[options.idempotency_key]={input,out};
    return out;
  }
 public:
  // Low-level service invocation is retained for existing callers. Application-bound
  // execution additionally checks the lifecycle before any cache or service access.
  Execution run(kloks::ServiceRegistry& services,const Values& input={},ExecutionOptions options={}) {
    return execute([&services](const std::string& service,const std::string& operation,
                               const kloks::Value& value){return services.call(service,operation,value);},
                   input,std::move(options));
  }
  Execution run(Application& app,const Values& input={},ExecutionOptions options={}) {
    if(app.state()!=ApplicationState::running)
      throw Error(ErrorCode::invalid_state,"workflow","application is not running");
    return execute([&app](const std::string& service,const std::string& operation,
                          const kloks::Value& value){return app.invoke(service,operation,value);},
                   input,std::move(options));
  }
};
}
