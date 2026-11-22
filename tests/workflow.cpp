#include <skipo/workflow.hpp>
#include <cassert>
#include <atomic>
#include <cstdint>
#include <stdexcept>
int main(){
  kloks::ServiceRegistry services;
  int calls=0;
  kloks::ServiceDefinition source;source.id="source";source.name="Source";source.version="1";
  source.operations["parse"]=[](const kloks::Value& v,const kloks::ServiceContext&){
    if(!std::holds_alternative<std::int64_t>(v))throw std::invalid_argument("number required");
    return v;
  };
  kloks::ServiceDefinition sink;sink.id="sink";sink.name="Sink";sink.version="1";sink.dependencies={"source"};
  sink.operations["double"]=[&](const kloks::Value& v,const kloks::ServiceContext&){
    ++calls;return kloks::Value{std::get<std::int64_t>(v)*2};
  };
  services.register_service(std::move(source));services.register_service(std::move(sink));
  skipo::Workflow pipeline;
  pipeline.add({"source","source","parse",{},nullptr,1,false});
  pipeline.add({"sink","sink","double",{"source"},
    [](const skipo::Values& previous,const skipo::Values&){return previous.at("source");},1,false});
  auto ok=pipeline.run(services,{{"source",std::int64_t{21}}},{"corr","once"});
  assert(ok.state==skipo::ExecutionState::completed&&std::get<std::int64_t>(ok.output.at("sink"))==42);
  assert(ok.traces.size()==2&&calls==1);
  auto duplicate=pipeline.run(services,{{"source",std::int64_t{21}}},{"corr","once"});
  assert(duplicate.from_idempotency_cache&&calls==1);
  try{(void)pipeline.run(services,{{"source",std::int64_t{22}}},{"corr","once"});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::idempotency_conflict);}
  auto invalid=pipeline.run(services,{{"source",std::string{"bad"}}});
  assert(invalid.state==skipo::ExecutionState::failed&&invalid.failure.has_value()&&calls==1);
  std::atomic_bool cancel{true};
  auto cancelled=pipeline.run(services,{},{"cancel","",&cancel});
  assert(cancelled.state==skipo::ExecutionState::cancelled);
  skipo::Workflow cycle;cycle.add({"a","source","parse",{"b"},nullptr,1,false});
  cycle.add({"b","sink","double",{"a"},nullptr,1,false});
  try{cycle.validate();assert(false);}catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::cycle);}
  try{skipo::Workflow unsafe;unsafe.add({"retry","source","parse",{},nullptr,2,false});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_definition);}
  return 0;
}
