#include <skipo/host.hpp>
#include <cassert>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
struct Memory : skipo::Workspace {
  std::map<std::string,std::vector<std::uint8_t>> files;
  std::vector<std::uint8_t> read(const std::string& key) override {
    if(!files.count(key))throw std::runtime_error("missing key");
    return files.at(key);
  }
  void write(const std::string& key,const std::vector<std::uint8_t>& value)override{files[key]=value;}
};
struct Config : skipo::Configuration {std::string get(const std::string& key)override{
  if(key=="APP_URL")return "native://app";
  throw std::runtime_error("unknown configuration");
}};
struct Clock : skipo::Clock {std::int64_t monotonic_millis() override{return 1123;}};
struct Remote : skipo::RemoteCapabilities {
  kloks::Value invoke(const std::string& service,const std::string& op,const kloks::Value& input)override {
    if(service!="math"||op!="echo")throw skipo::Error(skipo::ErrorCode::unauthorized,"remote","capability forbidden");
    return input;
  }
};
struct Http : skipo::Transport {
  int status{200};
  skipo::Response request(const skipo::Request&)override {return {status,{42}};}
};
int main() {
  skipo::HostBindings empty;
  try{(void)empty.read_workspace("k");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  try{(void)empty.config_value("APP_URL");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  try{(void)empty.call_remote("math","echo");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_unavailable);}
  Memory memory;Config config;Clock clock;Remote remote;Http http;
  skipo::HostBindings h;h.workspace=&memory;h.configuration=&config;h.clock=&clock;h.remote=&remote;h.transport=&http;
  h.write_workspace("song",{1,2,3});
  assert(h.read_workspace("song")==std::vector<std::uint8_t>({1,2,3}));
  assert(h.config_value("APP_URL")=="native://app");
  assert(h.time_millis()==1123);
  assert(std::get<std::int64_t>(h.call_remote("math","echo",std::int64_t{9}))==9);
  assert(h.send({"GET","/ping",{}}).status==200);
  try{(void)h.read_workspace("missing");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_failed);}
  try{h.write_workspace("",{});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_definition);}
  try{(void)h.config_value("missing");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_failed);}
  try{(void)h.call_remote("math","forbidden");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::unauthorized);}
  try{(void)h.call_remote("","echo");assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::invalid_definition);}
  http.status=302;
  try{(void)h.send({"GET","/redirect",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_failed);}
  http.status=401;
  try{(void)h.send({"GET","/private",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::unauthorized);}
  http.status=500;
  try{(void)h.send({"GET","/error",{}});assert(false);}
  catch(const skipo::Error& e){assert(e.code()==skipo::ErrorCode::host_failed);}
  return 0;
}
