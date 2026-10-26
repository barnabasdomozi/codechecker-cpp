// Port of codechecker_server/api/authentication.py: the Authentication
// service: logging in and out, permissions and personal access tokens
// (server-authentication.md §2-§5). OAuth logins are not supported yet;
// no OAuth provider is ever enabled (server_config.h).

#ifndef CC_SERVER_API_AUTHENTICATION_HANDLER_H_
#define CC_SERVER_API_AUTHENTICATION_HANDLER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "api/thrift/authentication_types.h"
#include "api/thrift/codeCheckerAuthentication.h"
#include "api/thrift/shared_types.h"
#include "server/auth/permissions.h"
#include "server/server_context.h"

namespace cc::server::api {

// Answers the Authentication calls of one request. Errors are thrown as
// RequestFailed.
class AuthenticationHandler final : public codeCheckerAuthenticationIf {
 public:
  explicit AuthenticationHandler(const Request& request) : request_(request) {}

  AuthenticationHandler(const AuthenticationHandler&) = delete;
  AuthenticationHandler& operator=(const AuthenticationHandler&) = delete;

  void checkAPIVersion() override {}
  void getAuthParameters(HandshakeInformation& result) override;
  void getAcceptedAuthMethods(std::vector<std::string>& result) override;
  void getAccessControl(AccessControl& result) override;
  void performLogin(std::string& result, const std::string& auth_method,
                    const std::string& auth_string) override;
  void getOauthProviders(std::vector<std::string>& result) override;
  void createLink(std::string& result, const std::string& provider) override;
  bool destroySession() override;
  void getLoggedInUser(std::string& result) override;
  void getPermissions(std::vector<Permission::type>& result,
                      const std::string& scope) override;
  void getPermissionsForUser(std::vector<Permission::type>& result,
                             const std::string& scope,
                             const std::string& extra_params,
                             const PermissionFilter& filter) override;
  void getAuthorisedNames(AuthorisationList& result,
                          Permission::type permission,
                          const std::string& extra_params) override;
  bool addPermission(Permission::type permission, const std::string& auth_name,
                     bool is_group, const std::string& extra_params) override;
  bool removePermission(Permission::type permission,
                        const std::string& auth_name, bool is_group,
                        const std::string& extra_params) override;
  bool hasPermission(Permission::type permission,
                     const std::string& extra_params) override;
  void getTokens(SessionTokenDataList& result) override;
  void newToken(SessionTokenData& result,
                const std::string& description) override;
  bool removeToken(const std::string& token) override;
  void getPersonalAccessTokens(PersonalAccessTokenList& result) override;
  int16_t getMaxTokenExpiration() override;
  void newPersonalAccessToken(PersonalAccessToken& result,
                              const std::string& name,
                              const std::string& description,
                              int64_t expiration) override;
  bool removePersonalAccessToken(const std::string& name) override;

 private:
  // Returns the user name of the caller; "" for a guest.
  std::string User() const;

  // Returns where the permissions of `extra_params`, a JSON object with an
  // optional "productID", are checked (Python: __unpack_extra_params()).
  auth::PermissionPlace PlaceOf(const std::string& extra_params);

  // Returns who the caller is, for the permission checks.
  auth::Requester Requester() const {
    return {.session = request_.session,
            .authentication_enabled = request_.server->sessions().enabled()};
  }

  // Throws UNAUTHORIZED unless the caller may manage `permission`.
  void RequireManager(Permission::type permission,
                      const auth::PermissionPlace& place);

  // Throws UNAUTHORIZED if the caller is not logged in (Python:
  // __require_privilaged_access()).
  void RequireLoggedIn() const;

  Request request_;
};

}  // namespace cc::server::api

#endif  // CC_SERVER_API_AUTHENTICATION_HANDLER_H_
