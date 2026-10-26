#include "server/database/schema.h"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "api/thrift/shared_types.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "util/logging.h"
#include "util/result.h"

namespace cc::server::db {
namespace {

#include "server/database/schema_sql.inc"

constexpr util::Logger kLog("system");

// The Alembic revisions of the configuration database, oldest first; the
// last one is the head (server-database.md §6).
constexpr std::string_view kConfigRevisions[] = {
    "8268fc7ca7f4", "150800b30447", "bb5278995f41", "6b9f832d0b20",
    "4964142b58d2", "3335ff7593cc", "dec6feb991e6", "302693c76eb8",
    "126fa3f55e4b", "4db450cf38af", "cf025b6d7998", "7829789fc19c",
    "00099e8bc212", "f59dfe4623fa", "04cd4df82fb5", "7ed50f8b3fb8",
    "5e1501dfd333", "511b1b37de2e", "635389f535cd",
};

// The Alembic revisions of the product database, oldest first.
constexpr std::string_view kProductRevisions[] = {
    "82ca43f05c10", "4b38fa14c27b", "dd9c97ead24",  "101a9cb747de",
    "080349e895d7", "3793e361a752", "e89887e7d3f0", "9987aa593ca7",
    "39f9e96071c0", "c042e02cca99", "3e91d0612422", "6cb6a3a41967",
    "a79677f54e48", "5f8a443a51e5", "2185167f8568", "af5d8a21c1e4",
    "dabc6998b8f0", "a24461972d2e", "f8291ab1d6be", "ad2a567e513a",
    "fb356f0eefed", "75ae226b5d88", "9d956a0fae8d", "c3dad71f8e6b",
    "198654dac219", "24c9660f82b1", "a7f3c8e21b90", "b1be74589382",
    "6c3c93a826f4", "feb5f01b5b52",
};

// Returns the revisions of the schema `kind`, oldest first.
std::span<const std::string_view> Revisions(SchemaKind kind) {
  if (kind == SchemaKind::kConfig) return kConfigRevisions;
  return kProductRevisions;
}

// Opens `database` for a status check; logs and returns nullptr if that
// fails (Python: DBContext).
std::unique_ptr<Connection> OpenForCheck(const Database& database) {
  Result<std::unique_ptr<Connection>> connection = database.Open();
  if (!connection.ok()) {
    kLog.Error(std::format("Database connection failed to '{}':\n{}",
                           database.SafeConnectionString(),
                           connection.error().message));
    return nullptr;
  }
  return std::move(connection).value();
}

}  // namespace

std::string_view HeadRevision(SchemaKind kind) {
  return Revisions(kind).back();
}

Result<std::optional<std::string>> SchemaRevision(Connection& connection) {
  using Revision = Result<std::optional<std::string>>;
  // Alembic reads the table only if it exists.
  const std::string_view exists_sql =
      connection.dialect() == Dialect::kSqlite
          ? "SELECT name FROM sqlite_schema "
            "WHERE type = 'table' AND name = 'alembic_version'"
          : "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema = current_schema() "
            "AND table_name = 'alembic_version'";
  Result<std::optional<std::string>> table =
      QueryValue(connection, exists_sql, {});
  if (!table.ok()) return table;
  if (!table.value()) return Revision(std::nullopt);
  return QueryValue(connection, "SELECT version_num FROM alembic_version", {});
}

DBStatus::type SchemaStatus(Connection& connection, SchemaKind kind) {
  Result<std::optional<std::string>> revision = SchemaRevision(connection);
  if (!revision.ok()) {
    kLog.Error(revision.error().message);
    return DBStatus::FAILED_TO_CONNECT;
  }
  kLog.Debug(std::format("Schema revision in the database: {}",
                         revision.value().value_or("None")));
  if (!revision.value()) {
    kLog.Debug("Database schema should have been created!");
    return DBStatus::SCHEMA_MISSING;
  }
  if (*revision.value() == HeadRevision(kind)) return DBStatus::OK;
  kLog.Debug(
      "Database schema mismatch detected between the package and the "
      "database");
  const std::span<const std::string_view> revisions = Revisions(kind);
  if (std::ranges::find(revisions, *revision.value()) == revisions.end()) {
    kLog.Debug("Automatic schema upgrade is not possible!");
    return DBStatus::SCHEMA_MISMATCH_NO;
  }
  return DBStatus::SCHEMA_MISMATCH_OK;
}

Result<void> CreateSchema(Connection& connection, SchemaKind kind) {
  const bool sqlite = connection.dialect() == Dialect::kSqlite;
  const std::string_view schema =
      kind == SchemaKind::kConfig
          ? (sqlite ? kConfigSchemaSqlite : kConfigSchemaPostgresql)
          : (sqlite ? kProductSchemaSqlite : kProductSchemaPostgresql);
  return RunInTransaction(connection, [&connection, schema] {
    return connection.Execute(schema);
  });
}

DBStatus::type CheckSchema(const Database& database, SchemaKind kind) {
  const std::unique_ptr<Connection> connection = OpenForCheck(database);
  if (!connection) return DBStatus::FAILED_TO_CONNECT;
  return SchemaStatus(*connection, kind);
}

std::optional<std::string> SchemaVersion(const Database& database) {
  const std::unique_ptr<Connection> connection = OpenForCheck(database);
  if (!connection) return std::nullopt;
  Result<std::optional<std::string>> revision = SchemaRevision(*connection);
  if (!revision.ok()) {
    kLog.Debug(revision.error().message);
    return std::nullopt;
  }
  return revision.value();
}

DBStatus::type ConnectDatabase(const Database& database, SchemaKind kind,
                               bool init) {
  if (init) {
    const std::unique_ptr<Connection> connection = OpenForCheck(database);
    if (!connection) return DBStatus::FAILED_TO_CONNECT;
    Result<std::optional<std::string>> revision = SchemaRevision(*connection);
    if (!revision.ok()) {
      kLog.Error(revision.error().message);
      return DBStatus::SCHEMA_INIT_ERROR;
    }
    if (!revision.value()) {
      kLog.Debug("No schema was detected in the database.");
      kLog.Debug("Initializing new ...");
      Result<void> created = CreateSchema(*connection, kind);
      if (!created.ok()) {
        kLog.Error("Failed to create initial database schema");
        kLog.Error(created.error().message);
        return DBStatus::SCHEMA_INIT_ERROR;
      }
      kLog.Debug("Done.");
    }
  }
  return CheckSchema(database, kind);
}

DBStatus::type UpgradeSchema(const Database& database, SchemaKind kind,
                             std::string_view name) {
  const DBStatus::type status = CheckSchema(database, kind);
  if (status == DBStatus::SCHEMA_MISSING) {
    return ConnectDatabase(database, kind, /*init=*/true) == DBStatus::OK
               ? DBStatus::OK
               : DBStatus::SCHEMA_UPGRADE_FAILED;
  }
  if (status != DBStatus::SCHEMA_MISMATCH_OK) return status;
  kLog.Error(std::format(
      "The database of '{}' has the schema revision '{}', which this server "
      "cannot upgrade. Upgrade it to revision '{}' with CodeChecker 6.30.0 "
      "first.",
      name, SchemaVersion(database).value_or("None"), HeadRevision(kind)));
  return DBStatus::SCHEMA_UPGRADE_FAILED;
}

}  // namespace cc::server::db
