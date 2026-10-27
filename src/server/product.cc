#include "server/product.h"

#include <stdint.h>

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "api/thrift/shared_types.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "server/database/schema.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/result.h"

namespace cc::server {
namespace {

constexpr util::Logger kLog("server");

// Returns the product row `row` of a "SELECT id, endpoint, display_name,
// connection FROM products" query.
Product::Record RecordOf(const db::Row& row) {
  return {.id = row.Int(0),
          .endpoint = row.Text(1),
          .display_name = row.Text(2),
          .connection = row.Text(3)};
}

}  // namespace

Product::Product(Record record, const std::string& workspace)
    : record_(std::move(record)) {
  Result<db::Database> database =
      db::Database::FromConnectionString(record_.connection, workspace);
  if (database.ok()) {
    database_ =
        std::make_unique<db::LazyConnection>(std::move(database).value());
  } else {
    last_failure_ = database.error().message;
  }
}

void Product::Connect(bool init) {
  kLog.Debug(std::format("Checking '{}' database.", endpoint()));
  auto fail = [this](const std::string& message) {
    kLog.Error(std::format(
        "The database for product '{}' cannot be connected to:\n{}",
        endpoint(), message));
    status_ = DBStatus::FAILED_TO_CONNECT;
    last_failure_ = message;
  };
  if (!database_) {
    fail(last_failure_.value_or(""));
    return;
  }
  Result<db::Connection*> connection = database_->Get();
  if (!connection.ok()) {
    fail(connection.error().message);
    return;
  }
  Result<db::QueryResult> probe = connection.value()->Query("SELECT 1", {});
  if (!probe.ok()) {
    database_->Close();
    fail(probe.error().message);
    return;
  }
  status_ = db::SchemaStatus(*connection.value(), db::SchemaKind::kProduct);
  last_failure_.reset();
  if (status_ == DBStatus::SCHEMA_MISSING && init) {
    kLog.Debug("Initializing new database schema.");
    Result<void> created =
        db::CreateSchema(*connection.value(), db::SchemaKind::kProduct);
    if (!created.ok()) {
      kLog.Error("Failed to create initial database schema");
      kLog.Error(created.error().message);
      status_ = DBStatus::SCHEMA_INIT_ERROR;
      return;
    }
    status_ = db::SchemaStatus(*connection.value(), db::SchemaKind::kProduct);
  }
}

Result<db::Connection*> Product::Connection() {
  if (!database_) {
    return Result<db::Connection*>(
        Error{.message = last_failure_.value_or("")});
  }
  return database_->Get();
}

void Product::Teardown() {
  if (database_) database_->Close();
}

void ProductRegistry::Add(const Product::Record& record, bool init,
                          bool count_runs) {
  if (products_.contains(record.endpoint)) {
    kLog.Debug("This product is already configured!");
    return;
  }
  kLog.Debug(std::format("Setting up product '{}'", record.endpoint));
  auto product = std::make_unique<Product>(record, workspace_);
  product->Connect(/*init=*/false);
  if (product->db_status() == DBStatus::FAILED_TO_CONNECT) {
    kLog.Debug(std::format("Failed to connect to database for product '{}'",
                           record.endpoint));
    return;
  }
  if (product->db_status() == DBStatus::SCHEMA_MISSING && init) {
    kLog.Debug("Schema was missing in the database. Initializing new");
    product->Connect(/*init=*/true);
  }
  if (count_runs) {
    // The cached number of runs that the product list shows is made right
    // at every start (Python: add_product()).
    Result<db::Connection*> connection = product->Connection();
    Result<db::QueryResult> runs =
        connection.ok()
            ? connection.value()->Query("SELECT count(id) FROM runs", {})
            : Result<db::QueryResult>(connection.error());
    if (runs.ok()) {
      Result<db::QueryResult> updated = config_db_->Query(
          "UPDATE products SET num_of_runs = ? WHERE id = ?",
          {runs.value().rows.front().Int(0), record.id});
      if (!updated.ok()) kLog.Error(updated.error().message);
    } else {
      kLog.Error(runs.error().message);
    }
  }
  products_.emplace(record.endpoint, std::move(product));
}

Product* ProductRegistry::Get(const std::string& endpoint) {
  if (const auto found = products_.find(endpoint); found != products_.end()) {
    return found->second.get();
  }
  kLog.Debug(std::format(
      "Product with the given endpoint '{}' does not exist in the local "
      "cache. Try to get it from the database.",
      endpoint));
  Result<db::QueryResult> rows = config_db_->Query(
      "SELECT id, endpoint, display_name, connection FROM products "
      "WHERE endpoint = ? LIMIT 1",
      {endpoint});
  if (!rows.ok()) {
    kLog.Error(rows.error().message);
    return nullptr;
  }
  if (rows.value().rows.empty()) return nullptr;
  Add(RecordOf(rows.value().rows.front()), /*init=*/false,
      /*count_runs=*/false);
  const auto added = products_.find(endpoint);
  return added == products_.end() ? nullptr : added->second.get();
}

Result<void> ProductRegistry::Remove(const std::string& endpoint) {
  Product* product = Get(endpoint);
  if (product == nullptr) {
    return Result<void>(Error{.message = std::format(
        "The product with the given endpoint '{}' does not exist!",
        endpoint)});
  }
  kLog.Info(std::format("Disconnecting product '{}'", endpoint));
  product->Teardown();
  products_.erase(endpoint);
  return Result<void>();
}

void ProductRegistry::RemoveAllExcept(
    const std::vector<std::string>& endpoints) {
  std::vector<std::string> removed;
  for (const auto& [endpoint, product] : products_) {
    if (std::ranges::find(endpoints, endpoint) == endpoints.end()) {
      removed.push_back(endpoint);
    }
  }
  for (const std::string& endpoint : removed) (void)Remove(endpoint);
}

void ProductRegistry::TeardownAll() {
  for (const auto& [endpoint, product] : products_) product->Teardown();
}

Result<bool> ProductRegistry::IsDatabaseUsed(const db::ConnectionUrl& url) {
  // Python compares (driver, database, host, port) tuples; relative SQLite
  // paths are resolved against the workspace.
  auto key = [](const db::ConnectionUrl& used) {
    return std::tuple(used.driver, used.database, used.host, used.port);
  };
  Result<db::QueryResult> rows =
      config_db_->Query("SELECT connection FROM products", {});
  if (!rows.ok()) return Result<bool>(rows.error());
  std::vector<std::string> connections = {
      config_db_->database().ConnectionString()};
  for (const db::Row& row : rows.value().rows) {
    connections.push_back(row.Text(0));
  }
  for (const std::string& connection : connections) {
    const std::optional<db::ConnectionUrl> used =
        db::ParseConnectionUrl(connection);
    if (!used) continue;
    if (key(db::ResolveSqliteRelativePath(*used, workspace_)) == key(url)) {
      return Result<bool>(true);
    }
  }
  return Result<bool>(false);
}

Result<void> SetCachedRunData(db::LazyConnection& config_db, int64_t id,
                              const std::string& endpoint,
                              int64_t runs_change,
                              std::optional<int64_t> latest_storage_date) {
  if (runs_change != 0) {
    kLog.Info(std::format(
        "{}: Changing 'num_of_runs' in CONFIG database by {}{}.", endpoint,
        runs_change > 0 ? '+' : '-',
        runs_change > 0 ? runs_change : -runs_change));
    Result<db::QueryResult> updated = config_db.Query(
        "UPDATE products SET num_of_runs = num_of_runs + ? WHERE id = ?",
        {runs_change, id});
    if (!updated.ok()) return Result<void>(updated.error());
  }
  if (latest_storage_date) {
    Result<db::QueryResult> updated = config_db.Query(
        "UPDATE products SET latest_storage_date = ? WHERE id = ?",
        {util::FormatDatabaseTime(*latest_storage_date), id});
    if (!updated.ok()) return Result<void>(updated.error());
  }
  return Result<void>();
}

Result<std::vector<Product::Record>> LoadProducts(db::Connection& config_db) {
  using Records = Result<std::vector<Product::Record>>;
  Result<db::QueryResult> rows = config_db.Query(
      "SELECT id, endpoint, display_name, connection FROM products "
      "ORDER BY endpoint",
      {});
  if (!rows.ok()) return Records(rows.error());
  std::vector<Product::Record> records;
  for (const db::Row& row : rows.value().rows) {
    records.push_back(RecordOf(row));
  }
  return Records(std::move(records));
}

}  // namespace cc::server
