// Port of codechecker_web/shared/database_status.py: the texts of the
// database statuses (server-database.md §3.1).

#ifndef CC_WEB_DATABASE_STATUS_H_
#define CC_WEB_DATABASE_STATUS_H_

#include <string_view>

#include "api/thrift/shared_types.h"

namespace cc::web {

// The message for a status that has no message of its own (Python: the
// default of db_status_msg.get()).
inline constexpr std::string_view kUnknownDatabaseStatus =
    "Unknown database status";

// Returns the message of `status`, for example "Database is up to date."
// (Python: database_status.db_status_msg), or kUnknownDatabaseStatus.
std::string_view DatabaseStatusMessage(DBStatus::type status);

}  // namespace cc::web

#endif  // CC_WEB_DATABASE_STATUS_H_
