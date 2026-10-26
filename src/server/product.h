// Port of codechecker_server/product.py and the product handling of
// CCSimpleHttpServer in server.py: the products a server process is
// connected to (server.md §8).

#ifndef CC_SERVER_PRODUCT_H_
#define CC_SERVER_PRODUCT_H_

#include <stdint.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "api/thrift/shared_types.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "util/result.h"

namespace cc::server {

// A product: a database of analysis results with its own endpoint. The
// connection to its database is opened when it is needed and kept, so each
// process has its own. Not thread-safe.
class Product {
 public:
  // A product row of the configuration database (Python:
  // config_db_model.Product).
  struct Record {
    int64_t id = 0;
    std::string endpoint;
    std::string display_name;
    std::string connection;  // The SQLAlchemy URL.
  };

  // `workspace` is where relative SQLite paths are resolved.
  Product(Record record, const std::string& workspace);

  Product(const Product&) = delete;
  Product& operator=(const Product&) = delete;

  int64_t id() const { return record_.id; }
  const std::string& endpoint() const { return record_.endpoint; }
  const std::string& display_name() const { return record_.display_name; }

  // Returns the status of the database after the last Connect().
  DBStatus::type db_status() const { return status_; }

  // Returns why the last Connect() failed; nullopt if it did not fail.
  const std::optional<std::string>& last_connection_failure() const {
    return last_failure_;
  }

  // Connects to the database and checks its schema; with `init`, a missing
  // schema is created (Python: Product.connect()). Updates db_status().
  void Connect(bool init);

  // Returns the connection to the database, opened again if Teardown()
  // closed it.
  Result<db::Connection*> Connection();

  // Closes the connection to the database (Python: Product.teardown()).
  void Teardown();

 private:
  Record record_;
  // Null if the connection string names no supported database.
  std::unique_ptr<db::LazyConnection> database_;
  DBStatus::type status_ = DBStatus::MISSING;
  std::optional<std::string> last_failure_;
};

// The products a server process uses, by endpoint (Python: the products of
// CCSimpleHttpServer). Not thread-safe.
class ProductRegistry {
 public:
  // Keeps `config_db`, which must outlive the registry. `workspace` is the
  // server's workspace folder.
  ProductRegistry(db::LazyConnection* config_db, std::string workspace)
      : config_db_(config_db), workspace_(std::move(workspace)) {}

  ProductRegistry(const ProductRegistry&) = delete;
  ProductRegistry& operator=(const ProductRegistry&) = delete;

  // Connects to the product `record` and keeps it, unless it is kept
  // already. A product whose database cannot be connected to is not kept.
  // With `init`, a missing schema is created. With `count_runs`, the
  // product's num_of_runs in the configuration database is set to its real
  // number of runs (Python: add_product()).
  void Add(const Product::Record& record, bool init, bool count_runs);

  // Returns the product with `endpoint`, connected first from the
  // configuration database if it is not kept yet; null if there is no such
  // product (Python: get_product()).
  Product* Get(const std::string& endpoint);

  // Disconnects the product with `endpoint` and forgets it (Python:
  // remove_product()). Fails if there is no such product.
  Result<void> Remove(const std::string& endpoint);

  // Removes every product except those in `endpoints` (Python:
  // remove_products_except()).
  void RemoveAllExcept(const std::vector<std::string>& endpoints);

  // Returns the number of products kept (Python: num_products).
  size_t size() const { return products_.size(); }

  // Closes the database connections of every product.
  void TeardownAll();

  // Returns whether the database at `url` is the configuration database or
  // the database of a product (Python: is_database_used()).
  Result<bool> IsDatabaseUsed(const db::ConnectionUrl& url);

 private:
  db::LazyConnection* config_db_;
  std::string workspace_;
  std::map<std::string, std::unique_ptr<Product>> products_;
};

// Returns the products of the configuration database, ordered by endpoint.
Result<std::vector<Product::Record>> LoadProducts(db::Connection& config_db);

}  // namespace cc::server

#endif  // CC_SERVER_PRODUCT_H_
