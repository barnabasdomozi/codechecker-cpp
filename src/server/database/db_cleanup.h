// Port of codechecker_server/database/db_cleanup.py and the start-up
// clean-up of server.py: removing expired and unused data from the product
// databases (server-database.md §5).

#ifndef CC_SERVER_DATABASE_DB_CLEANUP_H_
#define CC_SERVER_DATABASE_DB_CLEANUP_H_

#include <string>
#include <vector>

#include "common/checker_labels.h"
#include "server/product.h"

namespace cc::server::db {

// Cleans the database of every product in `products` in parallel, one job
// per CPU at most, and logs the products where it could not be done
// (Python: _do_db_cleanups() in server.py). `workspace` is where relative
// SQLite paths are resolved; the checker severities are updated from
// `labels`.
void CleanupProductDatabases(const std::vector<Product::Record>& products,
                             const std::string& workspace,
                             const common::CheckerLabels& labels);

}  // namespace cc::server::db

#endif  // CC_SERVER_DATABASE_DB_CLEANUP_H_
