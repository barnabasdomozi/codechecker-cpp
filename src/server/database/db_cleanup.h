// Port of codechecker_server/database/db_cleanup.py and the start-up
// clean-up of server.py: removing expired and unused data from the product
// databases (server-database.md §5).

#ifndef CC_SERVER_DATABASE_DB_CLEANUP_H_
#define CC_SERVER_DATABASE_DB_CLEANUP_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "common/checker_labels.h"
#include "server/database/connection.h"
#include "server/product.h"

namespace cc::server::db {

// Run locks younger than this belong to a store in progress; older ones are
// left behind by failed stores (Python: RUN_LOCK_TIMEOUT_IN_DATABASE).
inline constexpr int64_t kRunLockTimeoutSeconds = 30 * 60;

// Cleans the database of every product in `products` in parallel, one job
// per CPU at most, and logs the products where it could not be done
// (Python: _do_db_cleanups() in server.py). `workspace` is where relative
// SQLite paths are resolved; the checker severities are updated from
// `labels`.
void CleanupProductDatabases(const std::vector<Product::Record>& products,
                             const std::string& workspace,
                             const common::CheckerLabels& labels);

// Removes the comments whose report hash no report has any more (Python:
// remove_unused_comments()). Failures are logged with the product's
// `endpoint`.
void RemoveUnusedComments(Connection& connection, std::string_view endpoint);

// Removes the analysis info that no run history and no report refers to
// (Python: remove_unused_analysis_info()). Failures are logged with the
// product's `endpoint`.
void RemoveUnusedAnalysisInfo(Connection& connection,
                              std::string_view endpoint);

}  // namespace cc::server::db

#endif  // CC_SERVER_DATABASE_DB_CLEANUP_H_
