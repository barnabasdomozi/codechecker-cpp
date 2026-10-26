// Port of codechecker_web/shared/database_status.py: the texts of the
// database statuses (server-database.md §3.1).

#ifndef CC_WEB_DATABASE_STATUS_H_
#define CC_WEB_DATABASE_STATUS_H_

#include <string_view>

#include "api/thrift/shared_types.h"

namespace cc::web {

// Returns the message of `status`, for example "Database is up to date."
// (Python: database_status.db_status_msg), or "Unknown database status".
std::string_view DatabaseStatusMessage(DBStatus::type status);

}  // namespace cc::web

#endif  // CC_WEB_DATABASE_STATUS_H_
