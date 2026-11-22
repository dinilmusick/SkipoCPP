#include <skipo/ui.hpp>
#include <kloks/native_adapters.hpp>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
int main(){
  struct Memory : skipo::Workspace {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> read(const std::string&) override{return bytes;}
    void write(const std::string&,const std::vector<std::uint8_t>& v)override{bytes=v;}
  } workspace;
  skipo::HostBindings hosts;hosts.workspace=&workspace;
  krnl::Service krnl_source({"source","Source","1",{"number"},{"memory"}});
  skipo::Application app({"real-app","1",{"number"},{"workspace"}},hosts);
  app.add_capability({"number",{}});
  krnl::OperationInfo info;info.name="read";info.description="Read number";
  auto registration=krnl_source.add_operation<kloks::Value,kloks::Value>(
    info,[](const kloks::Value&,const krnl::InvocationContext&,krnl::Cancellation)->krnl::Result<kloks::Value>{
      return kloks::Value{std::int64_t{8}};
    });
  assert(registration.ok());
  krnl::OperationInfo rejects;rejects.name="reject";rejects.description="Reject invalid requests";
  auto rejected=krnl_source.add_operation<kloks::Value,kloks::Value>(
    rejects,[](const kloks::Value&,const krnl::InvocationContext&,krnl::Cancellation)->krnl::Result<kloks::Value>{
      throw std::invalid_argument("invalid upstream input");
    });
  assert(rejected.ok());
  auto source=kloks::bind_krnl_service(krnl_source);
  app.add_service(std::move(source));
  kloks::ServiceDefinition second;second.id="sink";second.name="Sink";second.version="1";second.dependencies={"source"};
  second.operations["write"]=[&](const kloks::Value& input,const kloks::ServiceContext&) {
    auto n=std::get<std::int64_t>(input)*3;
    hosts.write_workspace("number.bin",{static_cast<std::uint8_t>(n)});
    return kloks::Value{n};
  };
  app.add_service(std::move(second));
  app.initialize();app.start();
  assert(std::get<std::int64_t>(app.invoke("source","read"))==8);
  skipo::Workflow workflow;
  workflow.add({"load","source","read",{},nullptr,1,false});
  workflow.add({"save","sink","write",{"load"},
    [](const skipo::Values& v,const skipo::Values&){return v.at("load");},1,false});
  floks::Registry components;components.register_component({"Button",{{"enabled",true}},{"activate"},"run"});
  floks::Tree tree(components);tree.create("run","Button");tree.set_root("run");
  frnl::SemanticRuntime semantic;
  frnl::ComponentDefinition status;status.name="WorkflowStatus";
  status.states["idle"]={};status.states["done"]={};
  semantic.register_component("status",std::move(status),"idle");
  int observed_events=0;
  auto subscription=semantic.on_event([&](const frnl::EventNotice& e){
    if(e.component_id=="run" && e.event=="activate") ++observed_events;
  });
  auto result=skipo::Execution{};
  {
    skipo::UiCommands ui(tree,semantic);
    ui.bind_button("run",[&]{
      result=workflow.run(app,{},{"ui-event","ui-write-once"});
      if(result.state==skipo::ExecutionState::completed) semantic.set_state("status","done");
    });
    assert(ui.activate("run"));
    assert(result.state==skipo::ExecutionState::completed);
    assert(std::get<std::int64_t>(result.output.at("save"))==24);
    assert(hosts.read_workspace("number.bin")==std::vector<std::uint8_t>{24});
    assert(semantic.has_component("run"));
    assert(semantic.current_state("status")=="done" && observed_events==1);
  }
  try{(void)hosts.send({"GET","/missing",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  try{(void)hosts.send({"","/malformed",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_definition);}
  skipo::Workflow broken;
  broken.add({"reject","source","reject",{},nullptr,1,false});
  auto bad=broken.run(app);
  assert(bad.state==skipo::ExecutionState::failed && bad.failure.has_value());
  struct BrokenTransport : skipo::Transport {
    skipo::Response request(const skipo::Request&)override {throw std::runtime_error("network offline");}
  } broken_transport;
  hosts.transport=&broken_transport;
  try{(void)hosts.send({"GET","/offline",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_failed);}
  struct UnauthorizedTransport : skipo::Transport {
    skipo::Response request(const skipo::Request&)override {return {403,{}};}
  } unauthorized;
  hosts.transport=&unauthorized;
  try{(void)hosts.send({"GET","/private",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::unauthorized);}
  app.stop();
  try{(void)workflow.run(app,{},{"cached","ui-write-once"});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_state);}
  app.dispose();
  // A second, independently installed native application validates the
  // provider stop contract. Returning explicit codes works in Release builds.
  int base_stops=0,child_attempts=0;
  skipo::Application teardown({"teardown","1",{}, {}});
  kloks::ServiceDefinition base;
  base.id="base";base.name="Base";base.version="1";
  base.on_stop=[&]{++base_stops;};
  teardown.add_service(std::move(base));
  kloks::ServiceDefinition child;
  child.id="child";child.name="Child";child.version="1";
  child.dependencies={"base"};
  child.on_stop=[&]{if(++child_attempts==1) throw std::runtime_error("host cleanup failed");};
  teardown.add_service(std::move(child));
  teardown.initialize();
  teardown.start();
  try {teardown.stop();return 41;}
  catch(const kloks::ServiceError& e){
    if(e.code()!=kloks::ErrorCode::host_failure||e.service()!="child")return 42;
  }
  if(teardown.state()!=skipo::ApplicationState::failed ||
     !teardown.services().started("child") || !teardown.services().started("base") ||
     base_stops!=0) return 43;
  // Explicit retry stops the failed dependent first, then its provider.
  teardown.stop();
  if(teardown.state()!=skipo::ApplicationState::stopped ||
     child_attempts!=2||base_stops!=1) return 44;
  teardown.dispose();
  if(teardown.state()!=skipo::ApplicationState::disposed) return 45;
  // Suspend failure must be visible, with stopped resources never claimed.
  int suspend_attempts=0, suspend_base_stops=0;
  skipo::Application suspender({"suspender","1",{}, {}});
  kloks::ServiceDefinition sbase;
  sbase.id="provider";sbase.name="Provider";sbase.version="1";
  sbase.on_stop=[&]{++suspend_base_stops;};
  suspender.add_service(std::move(sbase));
  kloks::ServiceDefinition schild;
  schild.id="dependent";schild.name="Dependent";schild.version="1";
  schild.dependencies={"provider"};
  schild.on_stop=[&]{if(++suspend_attempts==1)throw std::runtime_error("suspend stop failed");};
  suspender.add_service(std::move(schild));
  suspender.initialize();suspender.start();
  try{suspender.suspend();return 46;}
  catch(const kloks::ServiceError& e){if(e.code()!=kloks::ErrorCode::host_failure)return 47;}
  if(suspender.state()!=skipo::ApplicationState::failed ||
     suspend_base_stops!=0||!suspender.services().started("provider")) return 48;
  suspender.dispose();
  if(suspend_attempts!=2||suspend_base_stops!=1||
     suspender.state()!=skipo::ApplicationState::disposed) return 49;
  return 0;
}
