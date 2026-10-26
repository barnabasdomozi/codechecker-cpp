#include "server/api/configuration_handler.h"

#include <optional>
#include <string>

#include "api/thrift/shared_types.h"
#include "server/api/request_failed.h"
#include "server/database/database.h"
#include "util/base64.h"

namespace cc::server::api {

void ConfigurationHandler::getNotificationBannerText(std::string& result) {
  const std::optional<std::string> text =
      ValueOrThrow(request_.server->config_db().QueryValue(
          "SELECT config_value FROM server_configurations "
          "WHERE config_key = 'notification_banner_text'",
          {}));
  result = util::Base64Encode(text.value_or(""));
}

void ConfigurationHandler::setNotificationBannerText(
    const std::string& notification_b64) {
  // Python: __require_supermission().
  if (request_.server->sessions().enabled() && request_.session == nullptr) {
    throw MakeRequestFailed(ErrorCode::UNAUTHORIZED,
                            "You are not authorized to execute this action.");
  }
  if (request_.session != nullptr && !request_.session->is_root) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        "You are not authorized to modify the notification.");
  }
  const std::string notification = DecodeBase64(notification_b64);
  db::LazyConnection& config_db = request_.server->config_db();
  const std::optional<std::string> existing = ValueOrThrow(config_db.QueryValue(
      "SELECT id FROM server_configurations "
      "WHERE config_key = 'notification_banner_text'",
      {}));
  if (existing) {
    ValueOrThrow(config_db.Query(
        "UPDATE server_configurations SET config_value = ? "
        "WHERE config_key = 'notification_banner_text'",
        {notification}));
  } else {
    ValueOrThrow(config_db.Query(
        "INSERT INTO server_configurations (config_key, config_value) "
        "VALUES ('notification_banner_text', ?)",
        {notification}));
  }
}

}  // namespace cc::server::api
