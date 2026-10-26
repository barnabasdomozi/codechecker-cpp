#include "server/auth/permissions.h"

#include <stdint.h>

#include <algorithm>
#include <deque>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/shared_types.h"
#include "server/auth/session.h"
#include "server/database/connection.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::server::auth {
namespace {

constexpr util::Logger kLog("server");

// The definition of a permission (Python: the _create_permission() calls at
// the end of permissions.py, with SUPERUSER added to the lists as the
// Permission constructor adds it).
struct Definition {
  Permission::type permission;
  const char* name;
  Scope scope;
  // The permissions that imply this one, in the order they are checked.
  std::span<const Permission::type> inherited_from;
  // The permissions that allow managing this one.
  std::span<const Permission::type> managed_by;
};

constexpr Permission::type kSuperuser[] = {Permission::SUPERUSER};
constexpr Permission::type kAdminManagers[] = {Permission::PRODUCT_ADMIN,
                                               Permission::SUPERUSER};
constexpr Permission::type kSuperuserAdmin[] = {Permission::SUPERUSER,
                                                Permission::PRODUCT_ADMIN};
constexpr Permission::type kAccessFrom[] = {Permission::SUPERUSER,
                                            Permission::PRODUCT_ADMIN,
                                            Permission::PRODUCT_STORE};
constexpr Permission::type kViewFrom[] = {
    Permission::SUPERUSER, Permission::PRODUCT_ACCESS,
    Permission::PRODUCT_ADMIN, Permission::PRODUCT_STORE};

// Ordered by the API value, as get_permissions() returns them.
constexpr Definition kDefinitions[] = {
    {Permission::SUPERUSER, "SUPERUSER", Scope::kSystem,
     std::span<const Permission::type>(), kSuperuser},
    {Permission::PERMISSION_VIEW, "PERMISSION_VIEW", Scope::kSystem,
     kSuperuser, kSuperuser},
    {Permission::PRODUCT_ADMIN, "PRODUCT_ADMIN", Scope::kProduct, kSuperuser,
     kAdminManagers},
    {Permission::PRODUCT_ACCESS, "PRODUCT_ACCESS", Scope::kProduct,
     kAccessFrom, kSuperuserAdmin},
    {Permission::PRODUCT_STORE, "PRODUCT_STORE", Scope::kProduct,
     kSuperuserAdmin, kSuperuserAdmin},
    {Permission::PRODUCT_VIEW, "PRODUCT_VIEW", Scope::kProduct, kViewFrom,
     kSuperuserAdmin},
};

constexpr Permission::type kSystemPermissions[] = {
    Permission::SUPERUSER, Permission::PERMISSION_VIEW};
constexpr Permission::type kProductPermissions[] = {
    Permission::PRODUCT_ADMIN, Permission::PRODUCT_ACCESS,
    Permission::PRODUCT_STORE, Permission::PRODUCT_VIEW};

// Returns the definition of `permission`; that of SUPERUSER for a value the
// API does not have.
const Definition& DefinitionOf(Permission::type permission) {
  for (const Definition& definition : kDefinitions) {
    if (definition.permission == permission) return definition;
  }
  return kDefinitions[0];
}

// The table of some permission records, the condition that selects them,
// and the parameters the condition needs.
struct RecordFilter {
  std::string table;
  std::string condition;
  std::vector<db::Param> params;
};

// Returns the filter of the records of `scope` at `place` that meet
// `condition`, which needs `params`. Product records are those of the
// product of `place`, or those of no product if it has none.
RecordFilter FilterAt(Scope scope, const PermissionPlace& place,
                      std::string condition, std::vector<db::Param> params) {
  RecordFilter filter = {.table = scope == Scope::kSystem
                                      ? "permissions_system"
                                      : "permissions_product",
                         .condition = std::move(condition),
                         .params = std::move(params)};
  if (scope == Scope::kSystem) return filter;
  // Python compares product_id with None, which SQLAlchemy writes as IS NULL.
  if (place.product_id) {
    filter.condition += " AND product_id = ?";
    filter.params.emplace_back(*place.product_id);
  } else {
    filter.condition += " AND product_id IS NULL";
  }
  return filter;
}

// Returns the filter of the records of `permission` at `place`.
RecordFilter FilterOf(Permission::type permission,
                      const PermissionPlace& place) {
  return FilterAt(ScopeOf(permission), place, "permission = ?",
                  {std::string(PermissionName(permission))});
}

// Returns whether a record grants `permission` at `place` to any of
// `names` as users or as groups (Python: _has_perm_impl()). Names are
// compared case-insensitively.
Result<bool> HasRecord(Permission::type permission,
                       const PermissionPlace& place,
                       const std::vector<std::string>& names, bool groups) {
  if (names.empty()) return Result<bool>(false);
  RecordFilter filter = FilterOf(permission, place);
  std::string placeholders;
  for (const std::string& name : names) {
    placeholders += placeholders.empty() ? "?" : ", ?";
    filter.params.emplace_back(util::AsciiLower(name));
  }
  filter.params.emplace_back(groups);
  Result<std::optional<std::string>> found = db::QueryValue(
      *place.config_db,
      std::format("SELECT 1 FROM {} WHERE {} AND lower(name) IN ({}) AND "
                  "is_group = ? LIMIT 1",
                  filter.table, filter.condition, placeholders),
      filter.params);
  if (!found.ok()) return Result<bool>(found.error());
  return Result<bool>(found.value().has_value());
}

// The permission records of a user or group.
struct StoredRecords {
  std::string name;  // The spelling in the records.
  std::vector<std::string> permissions;
};

// Returns the stored spelling of the user or group `name` and the
// permissions it has in the scope of `permission` (Python:
// __get_stored_auth_name_and_permissions()).
Result<StoredRecords> StoredNameAndRecords(Permission::type permission,
                                           const PermissionPlace& place,
                                           const std::string& name,
                                           bool is_group) {
  using Stored = Result<StoredRecords>;
  const RecordFilter filter =
      FilterAt(ScopeOf(permission), place, "lower(name) = ? AND is_group = ?",
               {util::AsciiLower(name), is_group});
  Result<db::QueryResult> rows = place.config_db->Query(
      std::format("SELECT name, permission FROM {} WHERE {}", filter.table,
                  filter.condition),
      filter.params);
  if (!rows.ok()) return Stored(rows.error());
  StoredRecords stored = {.name = name, .permissions = {}};
  for (const db::Row& row : rows.value().rows) {
    stored.name = row.Text(0);
    stored.permissions.push_back(row.Text(1));
  }
  return Stored(std::move(stored));
}

}  // namespace

std::optional<Scope> ScopeFromName(std::string_view name) {
  if (name == "SYSTEM") return Scope::kSystem;
  if (name == "PRODUCT") return Scope::kProduct;
  return std::nullopt;
}

std::span<const Permission::type> PermissionsOfScope(Scope scope) {
  if (scope == Scope::kSystem) return kSystemPermissions;
  return kProductPermissions;
}

std::string_view PermissionName(Permission::type permission) {
  return DefinitionOf(permission).name;
}

Scope ScopeOf(Permission::type permission) {
  return DefinitionOf(permission).scope;
}

Result<bool> HasPermission(Permission::type permission,
                           const PermissionPlace& place,
                           const Requester& requester) {
  // Guests have every permission on a server without authentication.
  if (requester.session == nullptr) {
    return Result<bool>(!requester.authentication_enabled);
  }
  if (requester.session->is_root && permission == Permission::SUPERUSER) {
    return Result<bool>(true);
  }
  Result<bool> by_name = HasRecord(permission, place,
                                   {requester.session->user},
                                   /*groups=*/false);
  if (!by_name.ok() || by_name.value()) return by_name;
  return HasRecord(permission, place, requester.session->groups,
                   /*groups=*/true);
}

Result<bool> RequirePermission(Permission::type permission,
                               const PermissionPlace& place,
                               const Requester& requester) {
  Result<bool> direct = HasPermission(permission, place, requester);
  if (!direct.ok() || direct.value()) return direct;
  // Python walks the inheritance breadth-first, visiting a permission again
  // if several paths lead to it.
  const std::span<const Permission::type> first =
      DefinitionOf(permission).inherited_from;
  std::deque<Permission::type> ancestors(first.begin(), first.end());
  while (!ancestors.empty()) {
    const Permission::type ancestor = ancestors.front();
    ancestors.pop_front();
    Result<bool> has = HasPermission(ancestor, place, requester);
    if (!has.ok() || has.value()) return has;
    for (const Permission::type next : DefinitionOf(ancestor).inherited_from) {
      ancestors.push_back(next);
    }
  }
  return Result<bool>(false);
}

Result<bool> RequireManager(Permission::type permission,
                            const PermissionPlace& place,
                            const Requester& requester) {
  for (const Permission::type manager : DefinitionOf(permission).managed_by) {
    Result<bool> has = HasPermission(manager, place, requester);
    if (!has.ok() || has.value()) return has;
  }
  return Result<bool>(false);
}

Result<PermittedNames> ListPermitted(Permission::type permission,
                                     const PermissionPlace& place) {
  RecordFilter filter = FilterOf(permission, place);
  Result<db::QueryResult> rows = place.config_db->Query(
      std::format("SELECT name, is_group FROM {} WHERE {} ORDER BY id",
                  filter.table, filter.condition),
      filter.params);
  if (!rows.ok()) return Result<PermittedNames>(rows.error());
  PermittedNames names;
  for (const db::Row& row : rows.value().rows) {
    (row.Bool(1) ? names.groups : names.users).push_back(row.Text(0));
  }
  return Result<PermittedNames>(std::move(names));
}

Result<void> AddPermission(Permission::type permission,
                           const PermissionPlace& place,
                           const std::string& name, bool is_group,
                           const std::string& actor) {
  const std::string_view kind = is_group ? "group" : "user";
  bool added = false;
  if (!name.empty()) {
    Result<StoredRecords> stored =
        StoredNameAndRecords(permission, place, name, is_group);
    if (!stored.ok()) return Result<void>(stored.error());
    const auto& [stored_name, records] = stored.value();
    if (std::ranges::find(records, PermissionName(permission)) ==
        records.end()) {
      const std::string& new_name = records.empty() ? name : stored_name;
      Result<db::QueryResult> inserted =
          ScopeOf(permission) == Scope::kSystem
              ? place.config_db->Query(
                    "INSERT INTO permissions_system (permission, name, "
                    "is_group) VALUES (?, ?, ?)",
                    {std::string(PermissionName(permission)), new_name,
                     is_group})
              : place.config_db->Query(
                    "INSERT INTO permissions_product (permission, "
                    "product_id, name, is_group) VALUES (?, ?, ?, ?)",
                    {std::string(PermissionName(permission)),
                     place.product_id ? db::Param(*place.product_id)
                                      : db::Param(nullptr),
                     new_name, is_group});
      if (!inserted.ok()) return Result<void>(inserted.error());
      added = true;
    }
  }
  if (added) {
    kLog.Info(std::format("Permission '{}' added for {} '{}' by '{}'.",
                          PermissionName(permission), kind, name, actor));
  } else {
    kLog.Info(std::format("Permission '{}' already added for {} '{}'!",
                          PermissionName(permission), kind, name));
  }
  return Result<void>();
}

Result<void> RemovePermission(Permission::type permission,
                              const PermissionPlace& place,
                              const std::string& name, bool is_group,
                              const std::string& actor) {
  const std::string_view kind = is_group ? "group" : "user";
  int64_t removed = 0;
  if (!name.empty()) {
    RecordFilter filter = FilterOf(permission, place);
    filter.params.emplace_back(util::AsciiLower(name));
    filter.params.emplace_back(is_group);
    Result<db::QueryResult> deleted = place.config_db->Query(
        std::format("DELETE FROM {} WHERE {} AND lower(name) = ? AND "
                    "is_group = ?",
                    filter.table, filter.condition),
        filter.params);
    if (!deleted.ok()) return Result<void>(deleted.error());
    removed = deleted.value().changed_rows;
  }
  if (removed > 0) {
    kLog.Info(std::format("Permission '{}' removed from {} '{}' by '{}'.",
                          PermissionName(permission), kind, name, actor));
  } else {
    kLog.Info(std::format("Permission '{}' already removed from {} '{}'!",
                          PermissionName(permission), kind, name));
  }
  return Result<void>();
}

Result<void> InitialiseDefaults(Scope scope, const PermissionPlace& place) {
  for (const Permission::type permission : PermissionsOfScope(scope)) {
    RecordFilter filter = FilterOf(permission, place);
    filter.params.emplace_back(false);
    Result<db::QueryResult> deleted = place.config_db->Query(
        std::format("DELETE FROM {} WHERE {} AND name = '*' AND is_group = ?",
                    filter.table, filter.condition),
        filter.params);
    if (!deleted.ok()) return Result<void>(deleted.error());
  }
  return Result<void>();
}

}  // namespace cc::server::auth
