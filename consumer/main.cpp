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
  skipo::Application app({"real-app","1",{"number"},{"workspace"}},hosts);
  app.add_capability({"number",{}});
  krnl::Service krnl_source({"source","Source","1",{"number"},{"memory"}});
  krnl::OperationInfo info;info.name="read";info.description="Read number";
  auto registration=krnl_source.register_operation(info,[](const std::any&,const krnl::InvocationContext&,krnl::Cancellation){
    return krnl::Result<std::any>(std::any(kloks::Value{std::int64_t{8}}));
  });
  assert(registration.ok());
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
  skipo::Workflow workflow;
  workflow.add({"load","source","read",{},nullptr,1,false});
  workflow.add({"save","sink","write",{"load"},
    [](const skipo::Values& v,const skipo::Values&){return v.at("load");},1,false});
  floks::Registry components;components.register_component({"Button",{{"enabled",true}},{"activate"},"run"});
  floks::Tree tree(components);tree.create("run","Button");tree.set_root("run");
  frnl::SemanticRuntime semantic;
  auto result=skipo::Execution{};
  {
    skipo::UiCommands ui(tree,semantic);
    ui.bind_button("run",[&]{result=workflow.run(app.services());});
    assert(ui.activate("run"));
    assert(result.state==skipo::ExecutionState::completed);
    assert(std::get<std::int64_t>(result.output.at("save"))==24);
    assert(workspace.read("number.bin")==std::vector<std::uint8_t>{24});
    assert(semantic.has_component("run"));
  }
  try{(void)hosts.require_transport().request({"GET","/missing",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  app.stop();app.dispose();
  return 0;
}
