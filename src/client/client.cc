#include "client/client.h"

#include <stdint.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "api/api_version.h"
#include "api/thrift/authentication_types.h"
#include "api/thrift/codeCheckerAuthentication.h"
#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/codeCheckerProductService.h"
#include "api/thrift/codeCheckerServersideTaskService.h"
#include "api/thrift/products_types.h"
#include "client/call/thrift_call.h"
#include "client/credentials/user_credentials.h"
#include "client/product.h"
#include "util/logging.h"
#include "util/result.h"
#include "web/env.h"

namespace cc::client {
namespace {

using credentials::UserCredentials;
using AuthClient = call::ServiceClient<codeCheckerAuthenticationClient>;
using Token = std::optional<std::string>;

constexpr util::Logger kLog("system");

// Returns the URL of the service `service` on the server of `url`: of the
// product of `url`, or server-wide if `url` names no product.
std::string ServiceUrl(const ProductUrl& url, std::string_view service) {
  const std::string path =
      url.product.empty()
          ? std::format("/v{}/{}", api::kApiVersion, service)
          : std::format("/{}/v{}/{}", url.product, api::kApiVersion, service);
  return CreateProductUrl(url.protocol, url.host, url.port, path);
}

// Returns a `ThriftClient` of the service at `service_url` that sends
// `token`. With `credentials`, a call refused for an expired session gets
// a new token from the server of `server` (Python: the get_new_token
// argument of the helpers).
template <typename ThriftClient>
Result<call::ServiceClient<ThriftClient>> MakeClient(
    const std::string& service_url, Token token, const ProductUrl& server,
    const UserCredentials* credentials);

// Logs in with the saved credentials if the server needs a login and there
// is no live session, and returns the new token; nullopt if no login is
// needed (Python: perform_auth_for_handler()).
Result<Token> PerformAuth(const ProductUrl& server, AuthClient& auth,
                          UserCredentials& credentials) {
  HandshakeInformation handshake;
  Result<void> called = auth.Call(
      "getAuthParameters",
      [&handshake](codeCheckerAuthenticationClient& client) {
        client.getAuthParameters(handshake);
      });
  if (!called.ok()) return Result<Token>(called.error());
  if (!handshake.requiresAuthentication || handshake.sessionStillActive) {
    return Result<Token>(Token());
  }
  const std::string password_file = web::PasswordFile();
  if (credentials.autologin()) {
    if (const Token auth_string =
            credentials.GetAuthString(server.host, server.port)) {
      kLog.Info("Logging in using pre-configured credentials...");
      const std::string user = auth_string->substr(0, auth_string->find(':'));
      if (user.empty()) {
        return Result<Token>(Error{
            .message = std::format(
                "No username supplied! Please specify the username in your "
                "\"{}\" file for {}:{}.",
                password_file, server.host, server.port)});
      }
      kLog.Debug(std::format("Trying to login as '{}' to '{}:{}'", user,
                             server.host, server.port));
      std::string token;
      called = auth.Call(
          "performLogin", [&](codeCheckerAuthenticationClient& client) {
            client.performLogin(token, "Username:Password", *auth_string);
          });
      if (!called.ok()) return Result<Token>(called.error());
      const Result<void> saved =
          credentials.SaveToken(server.host, server.port, token);
      if (!saved.ok()) return Result<Token>(saved.error());
      kLog.Info("Authentication successful.");
      return Result<Token>(Token(std::move(token)));
    }
    kLog.Error("Invalid pre-configured credentials.");
    kLog.Error(std::format(
        "Your password has been changed or personal access token has been "
        "removed which is used by your \"{}\" file. Please remove or change "
        "invalid credentials.",
        password_file));
  } else {
    kLog.Error("Access denied. This server requires authentication.");
    kLog.Error("Please log in onto the server using 'CodeChecker cmd login'.");
  }
  return Result<Token>(Error{});
}

// Returns a new session token from the server of `server` (Python:
// get_new_token()).
Result<Token> GetNewToken(const ProductUrl& server,
                          UserCredentials& credentials) {
  ProductUrl server_wide = server;
  server_wide.product.clear();
  Result<AuthClient> auth = MakeClient<codeCheckerAuthenticationClient>(
      ServiceUrl(server_wide, "Authentication"), Token(), server, nullptr);
  if (!auth.ok()) return Result<Token>(auth.error());
  return PerformAuth(server, auth.value(), credentials);
}

template <typename ThriftClient>
Result<call::ServiceClient<ThriftClient>> MakeClient(
    const std::string& service_url, Token token, const ProductUrl& server,
    const UserCredentials* credentials) {
  call::TokenSource get_new_token;
  if (credentials != nullptr) {
    // The function keeps its own copy, as the Python lambda keeps the
    // credential manager.
    UserCredentials saved = *credentials;
    get_new_token = [server, saved]() mutable {
      return GetNewToken(server, saved);
    };
  }
  Result<call::Connection> connection = call::Connection::Create(
      service_url, std::move(token), std::move(get_new_token));
  if (!connection.ok()) {
    return Result<call::ServiceClient<ThriftClient>>(connection.error());
  }
  return Result<call::ServiceClient<ThriftClient>>(
      call::ServiceClient<ThriftClient>(std::move(connection).value()));
}

// Returns the client of `service` (of the product of `url`, or server-wide)
// with the saved token of the server, or a token from a login with the
// saved credentials (the shared part of Python's setup_*_client()).
template <typename ThriftClient>
Result<call::ServiceClient<ThriftClient>> SetupServiceClient(
    const ProductUrl& url, std::string_view service) {
  using Client = call::ServiceClient<ThriftClient>;
  Result<UserCredentials> credentials = UserCredentials::Load();
  if (!credentials.ok()) return Result<Client>(credentials.error());
  Token token = credentials.value().GetToken(url.host, url.port);
  if (!token) {
    Result<Token> new_token = GetNewToken(url, credentials.value());
    if (!new_token.ok()) return Result<Client>(new_token.error());
    token = std::move(new_token).value();
  }
  return MakeClient<ThriftClient>(ServiceUrl(url, service), std::move(token),
                                  url, &credentials.value());
}

}  // namespace

Result<ResultsClient> SetupClient(const std::string& product_url) {
  const std::optional<ProductUrl> url = SplitProductUrl(product_url);
  if (!url) {
    return Result<ResultsClient>(Error{
        .message = "Malformed product URL was provided. A valid product URL "
                   "looks like this: 'http://my.server.com:80/ProductName'.",
        .exit_code = 2});
  }
  Result<ResultsClient> client =
      SetupServiceClient<codeCheckerDBAccessClient>(*url,
                                                    "CodeCheckerService");
  if (client.ok()) {
    kLog.Debug(std::format("Initializing client connecting to {}:{}/{} done.",
                           url->host, url->port, url->product));
  }
  return client;
}

Result<ProductClient> SetupProductClient(const ProductUrl& url) {
  Result<ProductClient> client =
      SetupServiceClient<codeCheckerProductServiceClient>(url, "Products");
  if (!client.ok()) return client;
  // The product may not exist, and then the server cannot be talked to in
  // order.
  std::string version;
  Result<void> called = client.value().Call(
      "getPackageVersion",
      [&version](codeCheckerProductServiceClient& products) {
        products.getPackageVersion(version);
      });
  if (!called.ok()) return Result<ProductClient>(called.error());
  if (version.empty()) {
    return Result<ProductClient>(Error{
        .message = std::format("The product '{}' cannot be communicated "
                               "with. It either doesn't exist, or the "
                               "server's configuration is bogus.",
                               url.product)});
  }
  Product product;
  called = client.value().Call(
      "getCurrentProduct",
      [&product](codeCheckerProductServiceClient& products) {
        products.getCurrentProduct(product);
      });
  if (!called.ok()) return Result<ProductClient>(called.error());
  return client;
}

Result<TaskClient> SetupTaskClient(std::string_view protocol,
                                   std::string_view host, int64_t port) {
  return SetupServiceClient<codeCheckerServersideTaskServiceClient>(
      ProductUrl{.protocol = std::string(protocol),
                 .host = std::string(host),
                 .port = port},
      "Tasks");
}

}  // namespace cc::client
