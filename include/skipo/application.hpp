#pragma once
#include <skipo/host.hpp>
#include <kloks/service.hpp>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace skipo {
enum class ApplicationState { created, initialized, running, suspended, stopped, failed, disposed };
struct Descriptor {
  std::string id, version;
  std::set<std::string> required_capabilities, required_hosts;
};
struct Capability {
  std::string id;
  std::vector<std::string> dependencies;
};
// Single-threaded caller-owned lifecycle; do not invoke register/start/stop concurrently
// or reenter lifecycle callbacks. All callbacks and borrowed host objects must outlive the app.
class Application {
  Descriptor descriptor_;
  HostBindings hosts_;
  kloks::ServiceRegistry services_;
  std::map<std::string,Capability> capabilities_;
  std::map<std::string,std::vector<std::string>> dependencies_;
  std::map<std::string,std::set<std::string>> provided_;
  std::vector<std::string> service_order_;
  ApplicationState state_{ApplicationState::created};
  bool changing_{false};
  // A capability declaration is a composition plan, never a virtual provider.
  // Resolution bottoms out in an actual Kloks service or injected host.
  bool has_provider(const std::string& id, std::set<std::string>& visiting) const {
    if(hosts_.available(id)) return true;
    for(const auto& [service, caps]:provided_) {
      (void)service;
      if(caps.count(id)) return true;
    }
    const auto it=capabilities_.find(id);
    if(it==capabilities_.end()||it->second.dependencies.empty()) return false;
    if(!visiting.insert(id).second)
      throw Error(ErrorCode::cycle,id,"capability provider cycle");
    for(const auto& dependency:it->second.dependencies) {
      if(!has_provider(dependency,visiting)) {
        visiting.erase(id);
        return false;
      }
    }
    visiting.erase(id);
    return true;
  }

  struct Changing {
    bool& flag;
    explicit Changing(bool& value):flag(value) { if(flag) throw Error(ErrorCode::invalid_state,"application","reentrant lifecycle");flag=true; }
    ~Changing(){flag=false;}
  };
  static std::vector<std::string> order(const std::map<std::string,std::vector<std::string>>& graph, const std::string& kind,
                                         std::function<bool(const std::string&)> external={}) {
    std::map<std::string,int> colors;
    std::vector<std::string> result;
    std::function<void(const std::string&)> visit=[&](const std::string& id) {
      if(colors[id]==2) return;
      if(colors[id]==1) throw Error(ErrorCode::cycle,id,kind+" dependency cycle");
      colors[id]=1;
      for(const auto& dep:graph.at(id)){
        if(!graph.count(dep)) {
          if(external&&external(dep))continue;
          throw Error(kind=="capability"?ErrorCode::missing_capability:ErrorCode::missing_service,
                      id,"unresolved "+kind+" dependency: "+dep);
        }
        visit(dep);
      }
      colors[id]=2;result.push_back(id);
    };
    for(const auto& item:graph) visit(item.first);
    return result;
  }
 public:
  explicit Application(Descriptor descriptor,HostBindings hosts={})
    : descriptor_(std::move(descriptor)),hosts_(hosts),services_(&hosts_) {
    if(descriptor_.id.empty()||descriptor_.version.empty())
      throw Error(ErrorCode::invalid_definition,"application","id and version required");
  }
  Application(const Application&)=delete;
  Application& operator=(const Application&)=delete;
  ~Application(){dispose();}
  ApplicationState state() const noexcept { return state_; }
  kloks::ServiceRegistry& services() noexcept { return services_; }
  const HostBindings& hosts() const noexcept { return hosts_; }
  kloks::Value invoke(const std::string& service,const std::string& operation,
                      const kloks::Value& input={}) {
    if(state_!=ApplicationState::running||changing_)
      throw Error(ErrorCode::invalid_state,"application","service invocation requires running application");
    return services_.call(service,operation,input);
  }
  void add_capability(Capability value) {
    if(state_!=ApplicationState::created) throw Error(ErrorCode::invalid_state,"capability","registration closed");
    if(value.id.empty()) throw Error(ErrorCode::invalid_definition,"capability","empty id");
    std::set<std::string> seen;
    for(const auto& dep:value.dependencies) {
      if(dep.empty()||dep==value.id)
        throw Error(ErrorCode::invalid_definition,value.id,"invalid capability dependency");
      if(!seen.insert(dep).second)
        throw Error(ErrorCode::duplicate,value.id,"duplicate capability dependency");
    }
    auto id=value.id;
    if(!capabilities_.emplace(id,std::move(value)).second)
      throw Error(ErrorCode::duplicate,id,"duplicate capability");
  }
  void add_service(kloks::ServiceDefinition value) {
    if(state_!=ApplicationState::created) throw Error(ErrorCode::invalid_state,"service","registration closed");
    if(value.id.empty()||dependencies_.count(value.id)) throw Error(ErrorCode::duplicate,value.id,"duplicate/empty service id");
    const auto id=value.id;
    const auto dependencies=value.dependencies;
    const auto capabilities=value.capabilities;
    services_.register_service(std::move(value));
    dependencies_[id]=dependencies;
    provided_[id]=capabilities;
  }
  void initialize() {
    if(state_!=ApplicationState::created) throw Error(ErrorCode::invalid_state,"application","initialize requires created");
    Changing guard(changing_);
    for(const auto& host:descriptor_.required_hosts)
      if(!hosts_.available(host)) throw Error(ErrorCode::host_unavailable,host,"required host is unavailable");
    std::map<std::string,std::vector<std::string>> cap_graph;
    for(const auto& [name,cap]:capabilities_) cap_graph[name]=cap.dependencies;
    (void)order(cap_graph,"capability",[this](const std::string& id) {
      std::set<std::string> visiting;
      return has_provider(id,visiting);
    });
    for(const auto& [name,cap]:capabilities_) {
      (void)cap;
      std::set<std::string> visiting;
      if(!has_provider(name,visiting))
        throw Error(ErrorCode::missing_capability,name,"declared capability has no concrete provider");
    }
    for(const auto& required:descriptor_.required_capabilities) {
      std::set<std::string> visiting;
      if(!has_provider(required,visiting))
        throw Error(ErrorCode::missing_capability,required,"required capability has no concrete provider");
    }
    service_order_=order(dependencies_,"service");
    state_=ApplicationState::initialized;
  }
  void start() {
    if(state_!=ApplicationState::initialized && state_!=ApplicationState::stopped)
      throw Error(ErrorCode::invalid_state,"application","start requires initialized or stopped");
    Changing guard(changing_);
    try {
      for(const auto& id:service_order_) services_.start(id);
      state_=ApplicationState::running;
    } catch(...) {
      services_.stop_all();
      state_=ApplicationState::failed;
      throw;
    }
  }
  void suspend() {
    if(state_!=ApplicationState::running) throw Error(ErrorCode::invalid_state,"application","suspend requires running");
    Changing guard(changing_);
    services_.stop_all(); state_=ApplicationState::suspended;
  }
  void resume() {
    if(state_!=ApplicationState::suspended) throw Error(ErrorCode::invalid_state,"application","resume requires suspended");
    Changing guard(changing_);
    try {for(const auto& id:service_order_)services_.start(id);state_=ApplicationState::running;}
    catch(...){services_.stop_all();state_=ApplicationState::failed;throw;}
  }
  void stop() noexcept {
    if(state_==ApplicationState::disposed||state_==ApplicationState::stopped||state_==ApplicationState::created)return;
    services_.stop_all();state_=ApplicationState::stopped;
  }
  void dispose() noexcept {
    if(state_==ApplicationState::disposed)return;
    stop();services_.dispose();state_=ApplicationState::disposed;
  }
};
}
