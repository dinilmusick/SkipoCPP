#include <skipo/application.hpp>
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
int main() {
  std::vector<std::string> log;
  skipo::Descriptor descriptor{"app","1.0",{"calculate"},{"workspace"}};
  try {skipo::Application unavailable(descriptor);unavailable.initialize();assert(false);} catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  struct Memory : skipo::Workspace {std::vector<std::uint8_t> read(const std::string&)override{return {1};} void write(const std::string&,const std::vector<std::uint8_t>&)override{}} memory;
  skipo::HostBindings hosts;hosts.workspace=&memory;
  skipo::Application app(descriptor,hosts);
  app.add_capability({"calculate",{}});
  kloks::ServiceDefinition first;first.id="first";first.name="First";first.version="1";
  first.capabilities.insert("calculate");
  first.on_start=[&]{log.push_back("first:start");};
  first.on_stop=[&]{log.push_back("first:stop");};
  first.operations["value"]=[](const kloks::Value&,const kloks::ServiceContext&){return kloks::Value{std::int64_t{7}};};
  app.add_service(std::move(first));
  kloks::ServiceDefinition second;second.id="second";second.name="Second";second.version="1";second.dependencies={"first"};
  second.on_start=[&]{log.push_back("second:start");};
  second.on_stop=[&]{log.push_back("second:stop");};
  second.operations["echo"]=[](const kloks::Value& value,const kloks::ServiceContext&){return value;};
  app.add_service(std::move(second));
  app.initialize();
  try{(void)app.invoke("second","echo",std::int64_t{10});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_state);}
  app.start();assert(log[0]=="first:start" && log[1]=="second:start");
  auto value=app.services().call("second","echo",std::int64_t{10});
  assert(std::get<std::int64_t>(value)==10);
  app.suspend();assert(log[2]=="second:stop" && log[3]=="first:stop");
  try{(void)app.invoke("second","echo",std::int64_t{10});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_state);}
  app.resume();
  assert(std::get<std::int64_t>(app.invoke("second","echo",std::int64_t{11}))==11);
  app.stop();app.stop();app.dispose();app.dispose();
  assert(app.state()==skipo::ApplicationState::disposed);
  skipo::Application cycle({"cycle","1",{}, {}});
  kloks::ServiceDefinition a;a.id="a";a.name="A";a.version="1";a.dependencies={"b"};
  kloks::ServiceDefinition b;b.id="b";b.name="B";b.version="1";b.dependencies={"a"};
  cycle.add_service(std::move(a));cycle.add_service(std::move(b));
  try{cycle.initialize();assert(false);}catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::cycle);}
  skipo::Application rollback({"fail","1",{}, {}});
  kloks::ServiceDefinition ok;ok.id="ok";ok.name="OK";ok.version="1";ok.on_start=[&]{log.push_back("ok:start");};
  ok.on_stop=[&]{log.push_back("ok:stop");};
  kloks::ServiceDefinition bad;bad.id="bad";bad.name="Bad";bad.version="1";bad.dependencies={"ok"};
  bad.on_start=[]{throw std::runtime_error("startup rejected");};
  rollback.add_service(std::move(ok));rollback.add_service(std::move(bad));
  rollback.initialize();
  try{rollback.start();assert(false);}catch(const std::runtime_error&){assert(rollback.state()==skipo::ApplicationState::failed);}
  assert(log.back()=="ok:stop");

  // Declaration without a concrete Kloks/host provider must never pass initialization.
  skipo::Application phantom({"phantom","1",{"ghost"},{}});
  phantom.add_capability({"ghost",{}});
  try{phantom.initialize();assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::missing_capability);}
  skipo::Application composed({"composed","1",{"summary"},{}});
  composed.add_capability({"summary",{"number"}});
  kloks::ServiceDefinition provider;provider.id="provider";provider.name="Provider";provider.version="1";
  provider.capabilities.insert("number");
  provider.operations["read"]=[](const kloks::Value&,const kloks::ServiceContext&){
    return kloks::Value{std::int64_t{15}};
  };
  composed.add_service(std::move(provider));
  composed.initialize();composed.start();
  assert(std::get<std::int64_t>(composed.invoke("provider","read"))==15);
  composed.dispose();
  skipo::Application broken_cap({"broken-cap","1",{},{}});
  broken_cap.add_capability({"a",{"missing"}});
  try{broken_cap.initialize();assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::missing_service);}
  skipo::Application duplicates({"duplicates","1",{},{}});
  try{duplicates.add_capability({"a",{"b","b"}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::duplicate);}
  return 0;
}
