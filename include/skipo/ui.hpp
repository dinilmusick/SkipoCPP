#pragma once
#include <skipo/workflow.hpp>
#include <floks/controls.hpp>
#include <floks/frnl_bridge.hpp>
#include <frnl/semantic_runtime.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace skipo {
// Frnl and Floks Tree are borrowed. Both outlive this controller.
// Dispatch is synchronous and caller-thread owned. No implicit GUI event loop.
class UiCommands {
  floks::Tree& tree_;
  frnl::SemanticRuntime& runtime_;
  floks::Controls controls_;
  floks::FrnlBridge bridge_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
  std::vector<floks::Tree::ScopedListener> listeners_;
 public:
  UiCommands(floks::Tree& tree,frnl::SemanticRuntime& runtime)
    :tree_(tree),runtime_(runtime),controls_(tree),bridge_(runtime){}
  UiCommands(const UiCommands&)=delete;
  UiCommands& operator=(const UiCommands&)=delete;
  ~UiCommands(){*alive_=false;listeners_.clear();}
  void bind_button(const std::string& id,std::function<void()> action) {
    if(!action) throw Error(ErrorCode::invalid_definition,id,"button requires an action");
    if(tree_.require(id)->type()!="Button")throw Error(ErrorCode::invalid_definition,id,"not a Floks Button");
    const std::weak_ptr<bool> token=alive_;
    bridge_.register_button(id,[token,action=std::move(action)] {
      if(const auto valid=token.lock();valid&&*valid) action();
    });
    auto* runtime=&runtime_;
    listeners_.push_back(tree_.on_scoped(id,"activate",[token,runtime,id](const floks::Event&){
      if(const auto valid=token.lock();valid&&*valid) (void)runtime->invoke(id,"activate");
    }));
  }
  bool activate(const std::string& id) {return controls_.activate_button(id);}
};
}
