#include <skipo/workflow.hpp>
#include <cassert>
#include <atomic>
#include <cstdint>
#include <stdexcept>
int main(){
  kloks::ServiceRegistry services;
  int calls=0, retries=0;
  kloks::ServiceDefinition source;source.id="source";source.name="Source";source.version="1";
  source.operations["parse"]=[](const kloks::Value& v,const kloks::ServiceContext&){
    if(!std::holds_alternative<std::int64_t>(v))throw std::invalid_argument("number required");
    return v;
  };
  kloks::ServiceDefinition sink;sink.id="sink";sink.name="Sink";sink.version="1";sink.dependencies={"source"};
  sink.operations["double"]=[&](const kloks::Value& v,const kloks::ServiceContext&){
    ++calls;return kloks::Value{std::get<std::int64_t>(v)*2};
  };
  sink.operations["flaky"]=[&](const kloks::Value& value,const kloks::ServiceContext&){
    if(++retries==1)throw kloks::ServiceError(kloks::ErrorCode::host_failure,"sink","transient failure");
    return value;
  };
  sink.operations["forbidden"]=[&](const kloks::Value&,const kloks::ServiceContext&)->kloks::Value {
    throw skipo::Error(skipo::ErrorCode::unauthorized,"sink","denied");
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

  skipo::Workflow retry;
  retry.add({"r","sink","flaky",{},nullptr,2,true});
  try{(void)retry.run(services,{{"r",std::int64_t{5}}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_definition);}
  auto retried=retry.run(services,{{"r",std::int64_t{5}}},{"retry-correlation","retry-id"});
  assert(retried.state==skipo::ExecutionState::completed&&retries==2);
  assert(retried.traces.size()==2&&!retried.traces[0].success&&retried.traces[1].success);
  auto reused=retry.run(services,{{"r",std::int64_t{5}}},{"new-correlation","retry-id"});
  assert(reused.from_idempotency_cache&&retries==2);
  std::atomic_bool abort_cached{true};
  auto abort=retry.run(services,{{"r",std::int64_t{5}}},{"abort","retry-id",&abort_cached});
  assert(abort.state==skipo::ExecutionState::cancelled&&!abort.from_idempotency_cache);
  skipo::Workflow denial;
  denial.add({"bad","sink","forbidden",{},nullptr,3,true});
  auto denied=denial.run(services,{},{"denied","denied-key"});
  assert(denied.state==skipo::ExecutionState::failed);
  assert(denied.failure&&denied.failure->code==skipo::ErrorCode::unauthorized&&denied.traces.size()==1);
  skipo::Workflow hidden;
  hidden.add({"a","source","parse",{},nullptr,1,false});
  hidden.add({"b","sink","double",{},[](const skipo::Values& previous,const skipo::Values&){
    return previous.at("a"); // invalid: undeclared predecessor must remain inaccessible
  },1,false});
  auto hidden_result=hidden.run(services,{{"a",std::int64_t{3}}});
  assert(hidden_result.state==skipo::ExecutionState::failed);
  assert(hidden_result.failure&&hidden_result.failure->step=="b");
  try{skipo::Workflow dup;dup.add({"a","sink","double",{"x","x"},nullptr,1,false});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::duplicate);}
  skipo::Application app({"native-app","1",{},{}});
  kloks::ServiceDefinition service;service.id="local";service.name="Local";service.version="1";
  service.operations["echo"]=[](const kloks::Value& v,const kloks::ServiceContext&){return v;};
  app.add_service(std::move(service));app.initialize();
  skipo::Workflow app_flow;app_flow.add({"echo","local","echo",{},nullptr,1,false});
  try{(void)app_flow.run(app,{{"echo",std::int64_t{7}}},{"app","cached"});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_state);}
  app.start();
  auto inside=app_flow.run(app,{{"echo",std::int64_t{7}}},{"app","cached"});
  assert(inside.state==skipo::ExecutionState::completed);
  app.stop();
  try{(void)app_flow.run(app,{{"echo",std::int64_t{7}}},{"app","cached"});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_state);}
  return 0;
}
