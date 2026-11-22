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
    hosts.require_workspace().write("number.bin",{static_cast<std::uint8_t>(n)});
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
      result=workflow.run(app.services());
      if(result.state==skipo::ExecutionState::completed) semantic.set_state("status","done");
    });
    assert(ui.activate("run"));
    assert(result.state==skipo::ExecutionState::completed);
    assert(std::get<std::int64_t>(result.output.at("save"))==24);
    assert(workspace.read("number.bin")==std::vector<std::uint8_t>{24});
    assert(semantic.has_component("run"));
    assert(semantic.current_state("status")=="done" && observed_events==1);
  }
  try{(void)hosts.send({"GET","/missing",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  try{(void)hosts.send({"","/malformed",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_definition);}
  skipo::Workflow broken;
  broken.add({"reject","source","reject",{},nullptr,1,false});
  auto bad=broken.run(app.services());
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
  app.stop();app.dispose();
  return 0;
}
