#include <skipo/ui.hpp>
#include <cassert>
int main(){
  floks::Registry registry;
  registry.register_component({"Button",{{"enabled",true}},{"activate"},"button"});
  floks::Tree tree(registry);
  tree.create("run","Button");tree.set_root("run");
  frnl::SemanticRuntime runtime;
  int calls=0,observed=0;
  auto listener=runtime.on_event([&](const frnl::EventNotice& e){
    if(e.component_id=="run"&&e.event=="activate") ++observed;
  });
  {
    skipo::UiCommands ui(tree,runtime);
    ui.bind_button("run",[&]{++calls;});
    assert(ui.activate("run"));
    assert(calls==1&&runtime.has_component("run")&&observed==1);
  }
  floks::Controls direct(tree);
  assert(direct.activate_button("run"));
  assert(calls==1&&observed==1);
  return 0;
}
