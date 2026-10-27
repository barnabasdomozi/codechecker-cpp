#include "server/api/authentication_handler.h"

#include <stdint.h>
#include <yyjson.h>

#include <format>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/authentication_types.h"
#include "api/thrift/shared_types.h"
#include "server/api/request_failed.h"
#include "server/auth/permissions.h"
#include "server/auth/session.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "server/server_context.h"
#include "util/datetime.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/uuid.h"

namespace cc::server::api {
namespace {

constexpr util::Logger kLog("server");

}  // namespace

std::string AuthenticationHandler::User() const {
  return request_.session ? request_.session->user : std::string();
}

auth::PermissionPlace AuthenticationHandler::PlaceOf(
    const std::string& extra_params) {
  std::optional<int64_t> product_id;
  if (!extra_params.empty()) {
    Result<util::JsonDocument> params = util::ParseJson(extra_params);
    if (!params.ok()) {
      throw MakeRequestFailed(ErrorCode::GENERAL, params.error().message);
    }
    if (yyjson_val* id = util::JsonGet(
            yyjson_doc_get_root(params.value().get()), "productID")) {
      product_id = util::JsonToInt(id);
    }
  }
  return {.config_db = ValueOrThrow(request_.server->config_db().Get()),
          .product_id = product_id};
}

void AuthenticationHandler::RequireManager(Permission::type permission,
                                           const auth::PermissionPlace& place) {
  if (!ValueOrThrow(auth::RequireManager(permission, place, Requester()))) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        std::format("You can not manage the permission '{}'",
                    auth::PermissionName(permission)));
  }
}

void AuthenticationHandler::RequireLoggedIn() const {
  if (User().empty()) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        "The server must be start by using privilaged access to execute this "
        "action.");
  }
}

void AuthenticationHandler::getAuthParameters(HandshakeInformation& result) {
  result.__set_requiresAuthentication(request_.server->sessions().enabled());
  result.__set_sessionStillActive(
      request_.session != nullptr &&
      request_.server->sessions().IsAlive(*request_.session));
}

void AuthenticationHandler::getAcceptedAuthMethods(
    std::vector<std::string>& result) {
  result = {"Username:Password", "oauth"};
}

void AuthenticationHandler::getAccessControl(AccessControl& result) {
  if (!hasPermission(Permission::PERMISSION_VIEW, "")) {
    throw MakeRequestFailed(ErrorCode::UNAUTHORIZED,
                            "You are not authorized to execute this action.");
  }
  db::LazyConnection& config_db = request_.server->config_db();
  Permissions global;
  global.__isset.user = global.__isset.group = true;
  for (const db::Row& row :
       ValueOrThrow(config_db.Query("SELECT name, permission, is_group FROM "
                                    "permissions_system ORDER BY id",
                                    {}))
           .rows) {
    (row.Bool(2) ? global.group : global.user)[row.Text(0)].push_back(
        row.Text(1));
  }
  cc::api::OrderedMap<std::string, Permissions> products;
  for (const db::Row& row :
       ValueOrThrow(config_db.Query(
                        "SELECT products.endpoint, permissions_product.name, "
                        "permissions_product.permission, "
                        "permissions_product.is_group FROM permissions_product "
                        "LEFT OUTER JOIN products "
                        "ON permissions_product.product_id = products.id "
                        "ORDER BY permissions_product.id",
                        {}))
           .rows) {
    Permissions& product = products[row.Text(0)];
    product.__isset.user = product.__isset.group = true;
    (row.Bool(3) ? product.group : product.user)[row.Text(1)].push_back(
        row.Text(2));
  }
  const std::optional<std::string>& super_user =
      request_.server->config().authentication().super_user;
  if (super_user && !super_user->empty()) {
    global.user[*super_user].push_back("SUPERUSER");
  }
  result.__set_globalPermissions(global);
  result.__set_productPermissions(products);
}

void AuthenticationHandler::performLogin(std::string& result,
                                         const std::string& auth_method,
                                         const std::string& auth_string) {
  if (auth_string.empty()) {
    throw MakeRequestFailed(
        ErrorCode::AUTH_DENIED,
        "No credentials supplied. Refusing authentication!");
  }
  if (auth_method == "Username:Password") {
    const std::string user = auth_string.substr(0, auth_string.find(':'));
    kLog.Debug(std::format("'{}' logging in...", user));
    const auth::Session* session = ValueOrThrow(
        request_.server->sessions().CreateSession(auth_string),
        ErrorCode::AUTH_DENIED);
    if (session != nullptr) {
      kLog.Info(std::format("'{}' logged in.", user));
      result = session->token;
      return;
    }
    std::string message = std::format(
        "Invalid credentials supplied for user '{}'. Refusing authentication!",
        user);
    const std::string& note =
        request_.server->config().authentication().failed_auth_message;
    if (!note.empty()) message += "\nNote: " + note;
    kLog.Warning(message);
    throw MakeRequestFailed(ErrorCode::AUTH_DENIED, message);
  }
  if (auth_method == "oauth") {
    // No OAuth provider is enabled on this server.
    kLog.Error(std::format(
        "OAuth authentication is not enabled for provider: {}",
        auth_string.substr(0, auth_string.find('@'))));
    throw MakeRequestFailed(ErrorCode::AUTH_DENIED,
                            "OAuth authentication is not enabled.");
  }
  kLog.Error("Could not negotiate via common authentication method.");
  throw MakeRequestFailed(
      ErrorCode::AUTH_DENIED,
      "Could not negotiate via common authentication method.");
}

void AuthenticationHandler::getOauthProviders(
    std::vector<std::string>& result) {
  result.clear();
}

void AuthenticationHandler::createLink(std::string& /*result*/,
                                       const std::string& provider) {
  kLog.Error(std::format(
      "OAuth createLink failed: OAuth authentication is not enabled for "
      "provider: {}",
      provider));
  throw MakeRequestFailed(ErrorCode::AUTH_DENIED, "createLink process failed.");
}

bool AuthenticationHandler::destroySession() {
  const std::string user = User();
  kLog.Debug(std::format("'{}' logging out...", user));
  // A guest has no session: the call then deletes nothing, as in Python.
  const bool logged_out = request_.server->sessions().Invalidate(
      request_.session ? request_.session->token : std::string());
  if (logged_out) kLog.Info(std::format("'{}' logged out.", user));
  return logged_out;
}

void AuthenticationHandler::getLoggedInUser(std::string& result) {
  result = User();
}

void AuthenticationHandler::getPermissions(
    std::vector<Permission::type>& result, const std::string& scope) {
  result.clear();
  if (const std::optional<auth::Scope> parsed = auth::ScopeFromName(scope)) {
    for (const Permission::type permission :
         auth::PermissionsOfScope(*parsed)) {
      result.push_back(permission);
    }
  }
}

void AuthenticationHandler::getPermissionsForUser(
    std::vector<Permission::type>& result, const std::string& scope,
    const std::string& extra_params, const PermissionFilter& filter) {
  getPermissions(result, scope);
  if (!filter.given && !filter.canManage) return;
  const auth::PermissionPlace place = PlaceOf(extra_params);
  std::vector<Permission::type> filtered;
  for (const Permission::type permission : result) {
    if (filter.given && !ValueOrThrow(auth::HasPermission(permission, place,
                                                          Requester()))) {
      continue;
    }
    if (filter.canManage && !ValueOrThrow(auth::RequireManager(
                                permission, place, Requester()))) {
      continue;
    }
    filtered.push_back(permission);
  }
  result = filtered;
}

void AuthenticationHandler::getAuthorisedNames(
    AuthorisationList& result, Permission::type permission,
    const std::string& extra_params) {
  const auth::PermissionPlace place = PlaceOf(extra_params);
  RequireManager(permission, place);
  auth::PermittedNames names =
      ValueOrThrow(auth::ListPermitted(permission, place));
  std::erase(names.users, "*");
  result.__set_users(names.users);
  result.__set_groups(names.groups);
}

bool AuthenticationHandler::addPermission(Permission::type permission,
                                          const std::string& auth_name,
                                          bool is_group,
                                          const std::string& extra_params) {
  const auth::PermissionPlace place = PlaceOf(extra_params);
  RequireManager(permission, place);
  ThrowIfFailed(auth::AddPermission(permission, place,
                                    std::string(util::Strip(auth_name)),
                                    is_group, User()));
  return true;
}

bool AuthenticationHandler::removePermission(Permission::type permission,
                                             const std::string& auth_name,
                                             bool is_group,
                                             const std::string& extra_params) {
  const auth::PermissionPlace place = PlaceOf(extra_params);
  RequireManager(permission, place);
  ThrowIfFailed(
      auth::RemovePermission(permission, place, auth_name, is_group, User()));
  return true;
}

bool AuthenticationHandler::hasPermission(Permission::type permission,
                                          const std::string& extra_params) {
  return ValueOrThrow(
      auth::RequirePermission(permission, PlaceOf(extra_params), Requester()));
}

void AuthenticationHandler::getTokens(SessionTokenDataList& result) {
  RequireLoggedIn();
  for (const db::Row& row :
       ValueOrThrow(request_.server->config_db().Query(
                        "SELECT description, last_access FROM "
                        "personal_access_tokens WHERE user_name = ? "
                        "ORDER BY id",
                        {User()}))
           .rows) {
    SessionTokenData token;
    token.__set_token("");
    if (!row.IsNull(0)) token.__set_description(row.Text(0));
    token.__set_lastAccess(util::DatabaseTimeToPython(row.Text(1)));
    result.push_back(token);
  }
}

void AuthenticationHandler::newToken(SessionTokenData& result,
                                     const std::string& description) {
  RequireLoggedIn();
  // A random name of 8 lowercase letters (Python: random.choice()).
  std::mt19937 random(std::random_device{}());
  std::uniform_int_distribution<int> letter(0, 25);
  std::string name;
  for (int i = 0; i < 8; ++i) name += static_cast<char>('a' + letter(random));
  PersonalAccessToken token;
  // Python calls newPersonalAccessToken() with its default expiration.
  newPersonalAccessToken(token, name, description, 365);
  result.__set_token(token.token);
  result.__set_description(description);
  result.__set_lastAccess(token.lastAccess);
}

bool AuthenticationHandler::removeToken(const std::string& token) {
  RequireLoggedIn();
  const int64_t removed =
      ValueOrThrow(request_.server->config_db().Query(
                       "DELETE FROM personal_access_tokens "
                       "WHERE user_name = ? AND token = ?",
                       {User(), token}))
          .changed_rows;
  if (removed == 0) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Personal access token {} was not found in the database.",
                    token));
  }
  request_.server->sessions().ForgetSession(token);
  kLog.Info(std::format("Personal access token '{}...' has been removed by "
                        "'{}'.",
                        token.substr(0, 5), User()));
  return true;
}

void AuthenticationHandler::getPersonalAccessTokens(
    PersonalAccessTokenList& result) {
  RequireLoggedIn();
  for (const db::Row& row :
       ValueOrThrow(request_.server->config_db().Query(
                        "SELECT token_name, description, last_access, "
                        "expiration FROM personal_access_tokens "
                        "WHERE user_name = ? ORDER BY id",
                        {User()}))
           .rows) {
    PersonalAccessToken token;
    token.__set_token("");
    token.__set_name(row.Text(0));
    if (!row.IsNull(1)) token.__set_description(row.Text(1));
    token.__set_lastAccess(util::DatabaseTimeToPython(row.Text(2)));
    token.__set_expiration(row.IsNull(3)
                               ? "None"
                               : util::DatabaseTimeToPython(row.Text(3)));
    result.push_back(token);
  }
}

int16_t AuthenticationHandler::getMaxTokenExpiration() {
  return static_cast<int16_t>(
      request_.server->config().authentication().max_token_expiration);
}

void AuthenticationHandler::newPersonalAccessToken(
    PersonalAccessToken& result, const std::string& name,
    const std::string& description, int64_t expiration) {
  RequireLoggedIn();
  const int64_t max_expiration =
      request_.server->config().authentication().max_token_expiration;
  if (expiration < 1 || expiration > max_expiration) {
    throw MakeRequestFailed(
        ErrorCode::GENERAL,
        std::format("Invalid expiration length. The length must be between 1 "
                    "and {}!",
                    max_expiration));
  }
  db::LazyConnection& config_db = request_.server->config_db();
  const db::QueryResult sessions = ValueOrThrow(config_db.Query(
      "SELECT user_name, groups, description FROM auth_sessions "
      "WHERE token = ? LIMIT 1",
      {request_.session->token}));
  if (sessions.rows.empty()) {
    throw MakeRequestFailed(ErrorCode::DATABASE,
                            "Invalid session token. Make sure to login to the "
                            "CodeChecker server first.");
  }
  const db::Row& session = sessions.rows.front();
  const auto optional_text = [&session](int column) {
    return session.IsNull(column) ? db::Param(nullptr)
                                  : db::Param(session.Text(column));
  };
  const int64_t now = util::LocalNow();
  const int64_t expires =
      now + expiration * 86400 * util::kMicrosecondsPerSecond;
  const std::optional<std::string> token = util::RandomHexToken(32);
  const std::optional<std::string> session_token = util::RandomHexToken(32);
  if (!token || !session_token) {
    throw MakeRequestFailed(ErrorCode::GENERAL, "Couldn't generate a token.");
  }
  const std::string token_name(util::Strip(name));
  db::Connection* connection = ValueOrThrow(config_db.Get());
  const Result<void> stored = db::RunInTransaction(*connection, [&] {
    // The token gets a session of its own, a copy of the caller's.
    Result<db::QueryResult> inserted = connection->Query(
        "INSERT INTO auth_sessions (user_name, token, groups, last_access, "
        "description) VALUES (?, ?, ?, ?, ?)",
        {optional_text(0), *session_token, optional_text(1),
         util::FormatDatabaseTime(now), optional_text(2)});
    if (inserted.ok()) {
      inserted = connection->Query(
          "INSERT INTO personal_access_tokens (user_name, token_name, token, "
          "description, groups, last_access, expiration, creation_date) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
          {User(), token_name, *token, description, optional_text(1),
           util::FormatDatabaseTime(now), util::FormatDatabaseTime(expires),
           util::FormatDatabaseTime(now)});
    }
    return inserted.ok() ? Result<void>() : Result<void>(inserted.error());
  });
  if (!stored.ok()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Personal access token with name '{}' already exists.",
                    token_name));
  }
  kLog.Info(std::format(
      "New personal access token {} has been generated by '{}'.", token_name,
      User()));
  result.__set_token(*token);
  result.__set_name(token_name);
  result.__set_description(description);
  result.__set_lastAccess(util::FormatPythonTime(now));
  result.__set_expiration(util::FormatPythonTime(expires));
}

bool AuthenticationHandler::removePersonalAccessToken(const std::string& name) {
  RequireLoggedIn();
  const int64_t removed =
      ValueOrThrow(request_.server->config_db().Query(
                       "DELETE FROM personal_access_tokens "
                       "WHERE user_name = ? AND token_name = ?",
                       {User(), name}))
          .changed_rows;
  if (removed == 0) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Personal access token with name '{}' was not found in "
                    "the database for the user '{}'.",
                    name, User()));
  }
  kLog.Info(std::format(
      "Personal access token with name '{}' has been removed by '{}'.", name,
      User()));
  return true;
}

}  // namespace cc::server::api
