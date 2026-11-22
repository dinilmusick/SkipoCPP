#pragma once
#include <kloks/service.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace skipo {
enum class ErrorCode { invalid_definition, duplicate, missing_capability, missing_service, cycle, invalid_state, cancelled, operation_failed, host_unavailable, host_failed, unauthorized, idempotency_conflict };
class Error : public std::runtime_error {
  ErrorCode code_;
  std::string source_;
 public:
  Error(ErrorCode code, std::string source, std::string message)
    : std::runtime_error(std::move(message)), code_(code), source_(std::move(source)) {}
  ErrorCode code() const noexcept { return code_; }
  const std::string& source() const noexcept { return source_; }
};
struct Request { std::string method, url; std::vector<std::uint8_t> body; };
struct Response { int status{0}; std::vector<std::uint8_t> body; };
struct Transport { virtual ~Transport()=default; virtual Response request(const Request&)=0; };
struct Workspace { virtual ~Workspace()=default; virtual std::vector<std::uint8_t> read(const std::string&)=0;
  virtual void write(const std::string&, const std::vector<std::uint8_t>&)=0; };
struct Configuration { virtual ~Configuration()=default; virtual std::string get(const std::string&)=0; };
struct Clock { virtual ~Clock()=default; virtual std::int64_t monotonic_millis()=0; };
struct RemoteCapabilities { virtual ~RemoteCapabilities()=default;
  virtual kloks::Value invoke(const std::string&, const std::string&, const kloks::Value&)=0; };
// All pointers are borrowed. Host retains ownership and outlives Application.
struct HostBindings {
  Transport* transport{nullptr};
  Workspace* workspace{nullptr};
  Configuration* configuration{nullptr};
  Clock* clock{nullptr};
  RemoteCapabilities* remote{nullptr};
  bool available(const std::string& capability) const noexcept {
    if(capability=="transport") return transport!=nullptr;
    if(capability=="workspace") return workspace!=nullptr;
    if(capability=="configuration") return configuration!=nullptr;
    if(capability=="clock") return clock!=nullptr;
    if(capability=="remote") return remote!=nullptr;
    return false;
  }
  Transport& require_transport() const { if(!transport) throw Error(ErrorCode::host_unavailable,"transport","transport not provided");return *transport; }
  Workspace& require_workspace() const { if(!workspace) throw Error(ErrorCode::host_unavailable,"workspace","workspace not provided");return *workspace; }
  Configuration& require_configuration() const { if(!configuration) throw Error(ErrorCode::host_unavailable,"configuration","configuration not provided");return *configuration; }
  Clock& require_clock() const { if(!clock) throw Error(ErrorCode::host_unavailable,"clock","clock not provided");return *clock; }
  RemoteCapabilities& require_remote() const { if(!remote) throw Error(ErrorCode::host_unavailable,"remote","remote capability provider not provided");return *remote; }
  std::vector<std::uint8_t> read_workspace(const std::string& key) const {
    if(key.empty()) throw Error(ErrorCode::invalid_definition,"workspace","workspace key required");
    try{return require_workspace().read(key);}
    catch(const Error&) {throw;}
    catch(const std::exception& e){throw Error(ErrorCode::host_failed,"workspace",e.what());}
    catch(...){throw Error(ErrorCode::host_failed,"workspace","unknown read failure");}
  }
  void write_workspace(const std::string& key,const std::vector<std::uint8_t>& bytes) const {
    if(key.empty()) throw Error(ErrorCode::invalid_definition,"workspace","workspace key required");
    try{require_workspace().write(key,bytes);}
    catch(const Error&) {throw;}
    catch(const std::exception& e){throw Error(ErrorCode::host_failed,"workspace",e.what());}
    catch(...){throw Error(ErrorCode::host_failed,"workspace","unknown write failure");}
  }
  std::string config_value(const std::string& key) const {
    if(key.empty()) throw Error(ErrorCode::invalid_definition,"configuration","configuration key required");
    try{return require_configuration().get(key);}
    catch(const Error&) {throw;}
    catch(const std::exception& e){throw Error(ErrorCode::host_failed,"configuration",e.what());}
    catch(...){throw Error(ErrorCode::host_failed,"configuration","unknown lookup failure");}
  }
  std::int64_t time_millis() const {
    try{return require_clock().monotonic_millis();}
    catch(const Error&) {throw;}
    catch(const std::exception& e){throw Error(ErrorCode::host_failed,"clock",e.what());}
    catch(...){throw Error(ErrorCode::host_failed,"clock","unknown clock failure");}
  }
  kloks::Value call_remote(const std::string& service,const std::string& operation,
                           const kloks::Value& input={}) const {
    if(service.empty()||operation.empty())
      throw Error(ErrorCode::invalid_definition,"remote","service and operation required");
    try{return require_remote().invoke(service,operation,input);}
    catch(const Error&) {throw;}
    catch(const std::exception& e){throw Error(ErrorCode::host_failed,"remote",e.what());}
    catch(...){throw Error(ErrorCode::host_failed,"remote","unknown remote failure");}
  }
  // Normalize host faults without replacing ownership or transport implementations.
  Response send(const Request& req) const {
    if(req.method.empty()||req.url.empty())
      throw Error(ErrorCode::invalid_definition,"transport","method and URL are required");
    try {
      auto response=require_transport().request(req);
      if(response.status==401||response.status==403)
        throw Error(ErrorCode::unauthorized,"transport","host rejected authorization");
      if(response.status<200||response.status>=300)
        throw Error(ErrorCode::host_failed,"transport","non-success host status "+std::to_string(response.status));
      return response;
    } catch(const Error&) {throw;}
    catch(const std::exception& error) {
      throw Error(ErrorCode::host_failed,"transport",error.what());
    } catch(...) {
      throw Error(ErrorCode::host_failed,"transport","unknown transport failure");
    }
  }
};
}
