// The schemas of the configuration and the product databases and their
// status checks (Python: the schema parts of SQLServer in
// codechecker_server/database/database.py; server-database.md §3, §6).
//
// Only the latest schema is supported: new databases are created with it
// directly, and the latest Alembic revision is written to alembic_version,
// as the Python version's migrations do. Upgrades from older revisions are
// not ported (libraries.md, "Database").

#ifndef CC_SERVER_DATABASE_SCHEMA_H_
#define CC_SERVER_DATABASE_SCHEMA_H_

#include <optional>
#include <string>
#include <string_view>

#include "api/thrift/shared_types.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "util/result.h"

namespace cc::server::db {

// Which of the two schemas a database has.
enum class SchemaKind {
  kConfig,   // The configuration database (config_db_model.py).
  kProduct,  // A product database (run_db_model.py).
};

// Returns the latest Alembic revision of the schema `kind` (Python: the head
// of the migration scripts).
std::string_view HeadRevision(SchemaKind kind);

// Returns the revision in the alembic_version table of `connection`;
// nullopt if there is none.
Result<std::optional<std::string>> SchemaRevision(Connection& connection);

// Returns the status of the schema of an open database: OK,
// SCHEMA_MISSING, SCHEMA_MISMATCH_OK (an older revision) or
// SCHEMA_MISMATCH_NO (an unknown, newer revision). FAILED_TO_CONNECT if it
// cannot be read.
DBStatus::type SchemaStatus(Connection& connection, SchemaKind kind);

// Creates the latest schema of `kind` in the empty database `connection`.
Result<void> CreateSchema(Connection& connection, SchemaKind kind);

// The functions below open their own connection to `database`. If that
// fails, they log "Database connection failed to '<url>':\n<error>" and
// return FAILED_TO_CONNECT (Python: DBContext).

// Returns the status of the schema of `database` (Python: check_schema()).
// A missing SQLite file is created, so it is SCHEMA_MISSING.
DBStatus::type CheckSchema(const Database& database, SchemaKind kind);

// Returns the revision of the schema of `database` (Python:
// get_schema_version()); nullopt if the database cannot be opened or has no
// schema.
std::optional<std::string> SchemaVersion(const Database& database);

// Python: SQLServer.connect(init): with `init`, creates the schema if there
// is none, then returns CheckSchema(). A failed creation is
// SCHEMA_INIT_ERROR.
DBStatus::type ConnectDatabase(const Database& database, SchemaKind kind,
                               bool init);

// Python: SQLServer.upgrade(): creates a missing schema; an older revision
// cannot be upgraded by the port, which logs why, naming the database
// `name`, and returns SCHEMA_UPGRADE_FAILED. Other statuses are returned
// as they are.
DBStatus::type UpgradeSchema(const Database& database, SchemaKind kind,
                             std::string_view name);

}  // namespace cc::server::db

#endif  // CC_SERVER_DATABASE_SCHEMA_H_
