// Port of codechecker_server/api/config_handler.py: the Configuration
// service, which holds the notification banner of the web UI (server-api.md
// §2.4).

#ifndef CC_SERVER_API_CONFIGURATION_HANDLER_H_
#define CC_SERVER_API_CONFIGURATION_HANDLER_H_

#include <string>

#include "api/thrift/configurationService.h"
#include "server/server_context.h"

namespace cc::server::api {

// Answers the Configuration calls of one request. Errors are thrown as
// RequestFailed.
class ConfigurationHandler final : public configurationServiceIf {
 public:
  explicit ConfigurationHandler(const Request& request) : request_(request) {}

  ConfigurationHandler(const ConfigurationHandler&) = delete;
  ConfigurationHandler& operator=(const ConfigurationHandler&) = delete;

  // Returns the notification banner text in Base64; "" if there is none.
  void getNotificationBannerText(std::string& result) override;

  // Sets the notification banner text from Base64. Needs a super user if
  // the server requires authentication.
  void setNotificationBannerText(const std::string& notification_b64) override;

 private:
  Request request_;
};

}  // namespace cc::server::api

#endif  // CC_SERVER_API_CONFIGURATION_HANDLER_H_
