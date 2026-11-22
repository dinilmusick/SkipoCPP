#include <skipo/ui.hpp>
#include <cassert>
int main(){
  floks::Registry registry;
  registry.register_component({"Button",{{"enabled",true}},{"activate"},"button"});
  floks::Tree tree(registry);
  tree.create("run","Button");tree.set_root("run");
  frnl::SemanticRuntime runtime;
  int calls=0;
  {
    skipo::UiCommands ui(tree,runtime);
    ui.bind_button("run",[&]{++calls;});
    assert(ui.activate("run"));
    assert(calls==1&&runtime.has_component("run"));
  }
  floks::Controls direct(tree);
  assert(direct.activate_button("run"));
  assert(calls==1);
  return 0;
}
