#include "web/database_status.h"

#include <string_view>

#include "api/thrift/shared_types.h"

namespace cc::web {

std::string_view DatabaseStatusMessage(DBStatus::type status) {
  switch (status) {
    case DBStatus::OK:
      return "Database is up to date.";
    case DBStatus::MISSING:
      return "Database is missing.";
    case DBStatus::FAILED_TO_CONNECT:
      return "Connection failed to the database server.";
    case DBStatus::SCHEMA_MISMATCH_OK:
      return "Database schema mismatch! Possible to upgrade.";
    case DBStatus::SCHEMA_MISMATCH_NO:
      return "Database schema mismatch! NOT possible to upgrade.";
    case DBStatus::SCHEMA_MISSING:
      return "Database schema is missing.";
    case DBStatus::SCHEMA_INIT_ERROR:
      return "Schema initialization error.";
    case DBStatus::SCHEMA_UPGRADE_FAILED:
      return "Schema upgrade failed.";
  }
  return "Unknown database status";
}

}  // namespace cc::web
