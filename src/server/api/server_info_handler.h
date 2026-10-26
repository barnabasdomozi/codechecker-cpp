// Port of codechecker_server/api/server_info_handler.py: the ServerInfo
// service (server-api.md §2.4).

#ifndef CC_SERVER_API_SERVER_INFO_HANDLER_H_
#define CC_SERVER_API_SERVER_INFO_HANDLER_H_

#include <string>

#include "api/thrift/serverInfoService.h"
#include "server/server_context.h"

namespace cc::server::api {

// Answers the ServerInfo calls of one request.
class ServerInfoHandler final : public serverInfoServiceIf {
 public:
  explicit ServerInfoHandler(const Request& request) : request_(request) {}

  ServerInfoHandler(const ServerInfoHandler&) = delete;
  ServerInfoHandler& operator=(const ServerInfoHandler&) = delete;

  // Returns the server's package version.
  void getPackageVersion(std::string& result) override {
    result = request_.server->package_version();
  }

 private:
  Request request_;
};

}  // namespace cc::server::api

#endif  // CC_SERVER_API_SERVER_INFO_HANDLER_H_
