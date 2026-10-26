// Port of codechecker_server/permissions.py: the permissions of the server,
// how they imply and manage each other, and the permission records of the
// configuration database (server-authentication.md §5).
//
// No permission is "default enabled" in the reference, so there are no
// "*" records to keep up; InitialiseDefaults() only removes stray ones.

#ifndef CC_SERVER_AUTH_PERMISSIONS_H_
#define CC_SERVER_AUTH_PERMISSIONS_H_

#include <stdint.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "api/thrift/shared_types.h"
#include "server/auth/session.h"
#include "server/database/connection.h"
#include "util/result.h"

namespace cc::server::auth {

// The scope of a permission (Python: PERMISSION_SCOPES).
enum class Scope { kSystem, kProduct };

// Returns the scope named `name` ("SYSTEM" or "PRODUCT"); nullopt for other
// names.
std::optional<Scope> ScopeFromName(std::string_view name);

// Returns the permissions of `scope`, ordered by their API value (Python:
// get_permissions(scope)).
std::span<const Permission::type> PermissionsOfScope(Scope scope);

// Returns the name of `permission`, for example "PRODUCT_ADMIN".
std::string_view PermissionName(Permission::type permission);

// Returns the scope of `permission`.
Scope ScopeOf(Permission::type permission);

// Who asks for a permission: the session of the request (null for a guest)
// and whether the server requires authentication.
struct Requester {
  const Session* session;
  bool authentication_enabled;
};

// Where a permission is checked: the configuration database and, for
// product permissions, the product's id (nullopt: none, so no product
// permission is granted).
struct PermissionPlace {
  db::Connection* config_db;
  std::optional<int64_t> product_id;
};

// Returns whether `requester` has `permission` directly (Python:
// PermissionHandler.has_permission()).
Result<bool> HasPermission(Permission::type permission,
                           const PermissionPlace& place,
                           const Requester& requester);

// Returns whether `requester` has `permission` directly or through a
// permission that implies it (Python: require_permission()).
Result<bool> RequirePermission(Permission::type permission,
                               const PermissionPlace& place,
                               const Requester& requester);

// Returns whether `requester` may manage `permission` (Python:
// require_manager()).
Result<bool> RequireManager(Permission::type permission,
                            const PermissionPlace& place,
                            const Requester& requester);

// The users and groups granted a permission.
struct PermittedNames {
  std::vector<std::string> users;
  std::vector<std::string> groups;
};

// Returns who is granted `permission` directly (Python: list_permitted()).
Result<PermittedNames> ListPermitted(Permission::type permission,
                                     const PermissionPlace& place);

// Grants `permission` to the user or group `name`, logging who did it
// (`actor`). An empty name and a permission granted already change nothing
// (Python: PermissionHandler.add_permission()).
Result<void> AddPermission(Permission::type permission,
                           const PermissionPlace& place,
                           const std::string& name, bool is_group,
                           const std::string& actor);

// Takes `permission` away from the user or group `name` (Python:
// PermissionHandler.remove_permission()).
Result<void> RemovePermission(Permission::type permission,
                              const PermissionPlace& place,
                              const std::string& name, bool is_group,
                              const std::string& actor);

// Removes the "*" records of the permissions of `scope` (Python:
// initialise_defaults()).
Result<void> InitialiseDefaults(Scope scope, const PermissionPlace& place);

}  // namespace cc::server::auth

#endif  // CC_SERVER_AUTH_PERMISSIONS_H_
