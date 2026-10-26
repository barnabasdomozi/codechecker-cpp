#include "server/api/product_handler.h"

#include <stdint.h>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/products_types.h"
#include "api/thrift/shared_types.h"
#include "server/api/request_failed.h"
#include "server/auth/permissions.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "server/http/routing.h"
#include "server/product.h"
#include "server/server_context.h"
#include "util/base64.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::server::api {
namespace {

constexpr util::Logger kLog("server");

// The columns of ProductRow, in its order.
constexpr std::string_view kProductColumns =
    "id, endpoint, connection, display_name, description, run_limit, "
    "is_review_status_change_disabled, latest_storage_date, num_of_runs, "
    "confidentiality, report_limit";

// Returns the LIKE pattern of a product filter: the LIKE characters of
// `filter` escaped with '\', and '*' as the wildcard (Python:
// conv(escape_like(filter, '\\'))).
std::string LikePattern(const std::string& filter) {
  std::string pattern;
  for (const char c : filter) {
    if (c == '\\' || c == '%' || c == '_') pattern += '\\';
    pattern += c == '*' ? '%' : c;
  }
  return pattern;
}

// Returns the confidentiality stored as `name` (Python:
// confidentiality_enum()); CONFIDENTIAL if there is none.
Confidentiality::type ConfidentialityOf(
    const std::optional<std::string>& name) {
  if (name == "INTERNAL") return Confidentiality::INTERNAL;
  if (name == "OPEN") return Confidentiality::OPEN;
  return Confidentiality::CONFIDENTIAL;
}

// Returns how `confidentiality` is stored (Python: confidentiality_str()).
db::Param ConfidentialityName(const ProductConfiguration& config) {
  if (!config.__isset.confidentiality) return db::Param(nullptr);
  switch (config.confidentiality) {
    case Confidentiality::CONFIDENTIAL:
      return db::Param(std::string("CONFIDENTIAL"));
    case Confidentiality::INTERNAL:
      return db::Param(std::string("INTERNAL"));
    case Confidentiality::OPEN:
      return db::Param(std::string("OPEN"));
  }
  return db::Param(nullptr);
}

// Logs `message` as an error and returns it as a RequestFailed with `code`.
::RequestFailed LoggedFailure(ErrorCode::type code,
                              const std::string& message) {
  kLog.Error(message);
  return MakeRequestFailed(code, message);
}

// Returns the conditions of the endpoint and display name filters of a
// product query, " AND <condition>" for each filter given, and appends their
// parameters to `params`.
std::string ProductFilters(const std::string& endpoint_filter,
                           const std::string& name_filter,
                           std::vector<db::Param>& params) {
  std::string conditions;
  if (!endpoint_filter.empty()) {
    conditions += " AND lower(endpoint) LIKE lower(?) ESCAPE '\\'";
    params.emplace_back(LikePattern(endpoint_filter));
  }
  if (!name_filter.empty()) {
    conditions += " AND lower(display_name) LIKE lower(?) ESCAPE '\\'";
    params.emplace_back(LikePattern(name_filter));
  }
  return conditions;
}

// Returns the SQLite path `path` relative to the folder `workspace` if it is
// under it, as getProductConfiguration() shows it: easier to edit.
std::string WithoutWorkspace(const std::string& path,
                             const std::string& workspace) {
  const std::string prefix = util::pypath::Normpath(workspace) + "/";
  return path.starts_with(prefix) ? path.substr(prefix.size()) : path;
}

// Returns whether `name` is a PostgreSQL database name that is safe to use
// in a quoted identifier (Python: is_valid_postgresql_db_name()).
bool IsValidPostgresqlDatabaseName(const std::string& name) {
  if (name.empty() || name.size() > 63) return false;
  constexpr std::string_view kForbidden("\"'\\;\0\r\n\t ", 9);
  return name.find_first_of(kForbidden) == std::string::npos;
}

// Returns the SQLAlchemy URL `connection` with a relative SQLite path made
// absolute against `workspace`; other URLs are returned unchanged (Python:
// SQLServer.resolve_sqlite_relative_path()).
std::string ResolvedConnectionString(const std::string& connection,
                                     const std::string& workspace) {
  const std::optional<db::ConnectionUrl> url =
      db::ParseConnectionUrl(connection);
  if (!url) return connection;
  const db::ConnectionUrl resolved =
      db::ResolveSqliteRelativePath(*url, workspace);
  if (resolved.database == url->database) return connection;
  return db::FormatConnectionUrl(resolved, /*hide_password=*/false);
}

// Returns an optional integer column of the API as a parameter.
db::Param OptionalInt(bool is_set, int64_t value) {
  return is_set ? db::Param(value) : db::Param(nullptr);
}

// Returns the display name of `config`; its endpoint if it has none.
std::string DisplayName(const ProductConfiguration& config) {
  if (config.displayedName_b64.empty()) return config.endpoint;
  return DecodeBase64(config.displayedName_b64);
}

// Returns the description of `config` as it is stored; NULL if it has none.
db::Param Description(const ProductConfiguration& config) {
  if (config.description_b64.empty()) return db::Param(nullptr);
  return db::Param(DecodeBase64(config.description_b64));
}

// Throws GENERAL "Database engine '<engine>' unknown!" unless `connection`
// is SQLite or PostgreSQL.
void RequireKnownEngine(const DatabaseConnection& connection) {
  if (connection.engine != "sqlite" && connection.engine != "postgresql") {
    throw LoggedFailure(
        ErrorCode::GENERAL,
        std::format("Database engine '{}' unknown!", connection.engine));
  }
}

// Connects `registry` to the product `record`, whose database is
// `connection`, to try its settings; with `init`, a missing schema is
// created. If that fails, throws what addProduct() or editProduct() (`verb`
// "add" or "edit") report: DATABASE if the product is not kept, or IOERROR
// naming `endpoint` after the product is removed again.
void ConnectProduct(ProductRegistry& registry,
                    const server::Product::Record& record, bool init,
                    const DatabaseConnection& connection,
                    std::string_view verb, const std::string& endpoint) {
  registry.Add(record, init, /*count_runs=*/false);
  const server::Product* product = registry.Get(record.endpoint);
  if (product == nullptr) {
    std::string message = std::format("Failed to {} product.", verb);
    if (connection.engine == "sqlite") {
      message +=
          " The SQLite database file must be under the server workspace "
          "directory.";
    }
    throw MakeRequestFailed(ErrorCode::DATABASE, message);
  }
  if (product->last_connection_failure()) {
    const std::string message =
        std::format("The configured connection for '/{}' failed: {}",
                    endpoint, *product->last_connection_failure());
    (void)registry.Remove(record.endpoint);
    throw LoggedFailure(ErrorCode::IOERROR, message);
  }
}

}  // namespace

ProductHandler::ProductRow ProductHandler::ToProductRow(const db::Row& row) {
  ProductRow product;
  product.id = row.Int(0);
  product.endpoint = row.Text(1);
  product.connection = row.Text(2);
  product.display_name = row.Text(3);
  product.description = row.OptionalText(4);
  product.run_limit = row.OptionalInt(5);
  if (!row.IsNull(6)) product.is_review_status_change_disabled = row.Bool(6);
  product.latest_storage_date = row.OptionalText(7);
  product.num_of_runs = row.Int(8);
  product.confidentiality = row.OptionalText(9);
  product.report_limit = row.Int(10);
  return product;
}

std::optional<ProductHandler::ProductRow> ProductHandler::LoadRow(int64_t id) {
  const db::QueryResult result = ValueOrThrow(config_db().Query(
      std::format("SELECT {} FROM products WHERE id = ?", kProductColumns),
      {id}));
  if (result.rows.empty()) return std::nullopt;
  return ToProductRow(result.rows.front());
}

ProductHandler::ProductRow ProductHandler::RequireRow(int64_t id) {
  std::optional<ProductRow> row = LoadRow(id);
  if (!row) {
    throw LoggedFailure(ErrorCode::DATABASE,
                        std::format("Product with ID {} does not exist!", id));
  }
  return *std::move(row);
}

bool ProductHandler::Has(Permission::type permission,
                         std::optional<int64_t> product_id) {
  db::Connection* connection = ValueOrThrow(config_db().Get());
  return ValueOrThrow(auth::RequirePermission(
      permission, {.config_db = connection, .product_id = product_id},
      {.session = request_.session,
       .authentication_enabled = request_.server->sessions().enabled()}));
}

void ProductHandler::Require(
    std::initializer_list<Permission::type> permissions,
    std::optional<int64_t> product_id) {
  for (const Permission::type permission : permissions) {
    if (Has(permission, product_id)) return;
  }
  throw MakeRequestFailed(ErrorCode::UNAUTHORIZED,
                          "You are not authorized to execute this action.");
}

::Product ProductHandler::ToApiProduct(const ProductRow& row) {
  ProductRegistry& registry = request_.server->products();
  server::Product* connected = registry.Get(row.endpoint);
  if (connected == nullptr) {
    kLog.Info(std::format(
        "Product '{}' was found in the configuration database but no "
        "database connection was present. Mounting analysis run database...",
        row.endpoint));
    registry.Add({.id = row.id,
                  .endpoint = row.endpoint,
                  .display_name = row.display_name,
                  .connection = row.connection},
                 /*init=*/false, /*count_runs=*/false);
    connected = registry.Get(row.endpoint);
  }
  const DBStatus::type status =
      connected ? connected->db_status() : DBStatus::FAILED_TO_CONNECT;

  Result<db::QueryResult> admins = config_db().Query(
      "SELECT name FROM permissions_product WHERE permission = "
      "'PRODUCT_ADMIN' AND product_id = ? ORDER BY id",
      {row.id});
  std::vector<std::string> admin_names;
  for (const db::Row& admin : ValueOrThrow(std::move(admins)).rows) {
    admin_names.push_back(admin.Text(0));
  }

  ::Product product;
  product.__set_id(row.id);
  product.__set_endpoint(row.endpoint);
  product.__set_displayedName_b64(util::Base64Encode(row.display_name));
  if (row.description && !row.description->empty()) {
    product.__set_description_b64(util::Base64Encode(*row.description));
  }
  product.__set_runCount(row.num_of_runs);
  if (row.latest_storage_date) {
    product.__set_latestStoreToProduct(
        util::DatabaseTimeToPython(*row.latest_storage_date));
  }
  product.__set_connected(status == DBStatus::OK);
  product.__set_accessible(Has(Permission::PRODUCT_VIEW, row.id) ||
                           Has(Permission::PERMISSION_VIEW, row.id));
  product.__set_administrating(Has(Permission::SUPERUSER, row.id) ||
                               Has(Permission::PRODUCT_ADMIN, row.id));
  product.__set_databaseStatus(status);
  product.__set_admins(admin_names);
  product.__set_confidentiality(ConfidentialityOf(row.confidentiality));
  product.__set_reportLimit(row.report_limit);
  return product;
}

void ProductHandler::getPackageVersion(std::string& result) {
  result = request_.server->package_version();
}

bool ProductHandler::isAdministratorOfAnyProduct() {
  const db::QueryResult products =
      ValueOrThrow(config_db().Query("SELECT id FROM products", {}));
  return std::ranges::any_of(products.rows, [this](const db::Row& row) {
    return Has(Permission::PRODUCT_ADMIN, row.Int(0));
  });
}

void ProductHandler::getProducts(Products& result,
                                 const std::string& endpoint_filter,
                                 const std::string& name_filter, int64_t limit,
                                 int64_t offset,
                                 ProductSortMode::type sorting_mode) {
  ProductRegistry& registry = request_.server->products();
  const db::QueryResult endpoints =
      ValueOrThrow(config_db().Query("SELECT endpoint FROM products", {}));
  if (endpoints.rows.size() < registry.size()) {
    // Another server with the same configuration database removed some.
    kLog.Info(std::format(
        "{} products were removed but server is still connected to them. "
        "Disconnecting these...",
        registry.size() - endpoints.rows.size()));
    std::vector<std::string> kept;
    for (const db::Row& row : endpoints.rows) kept.push_back(row.Text(0));
    registry.RemoveAllExcept(kept);
  }

  std::vector<db::Param> params;
  std::string sql = std::format(
      "SELECT {} FROM products WHERE 1 = 1{}", kProductColumns,
      ProductFilters(endpoint_filter, name_filter, params));
  // Every mode ends with the id, so pages neither repeat nor skip rows.
  switch (sorting_mode) {
    case ProductSortMode::NAMES_ASC:
      sql += " ORDER BY lower(display_name) ASC, id ASC";
      break;
    case ProductSortMode::NAMES_DESC:
      sql += " ORDER BY lower(display_name) DESC, id ASC";
      break;
    case ProductSortMode::RUNS_ASC:
      sql += " ORDER BY num_of_runs ASC, id ASC";
      break;
    case ProductSortMode::RUNS_DESC:
      sql += " ORDER BY num_of_runs DESC, id ASC";
      break;
    case ProductSortMode::LATEST_STORE_ASC:
      sql += " ORDER BY latest_storage_date ASC NULLS LAST, id ASC";
      break;
    case ProductSortMode::LATEST_STORE_DESC:
      sql += " ORDER BY latest_storage_date DESC NULLS LAST, id ASC";
      break;
    default: {
      // ACCESSRIGHT: the products the caller can view come first.
      const bool everything_visible =
          !request_.server->sessions().enabled() ||
          request_.session == nullptr ||
          Has(Permission::SUPERUSER, std::nullopt) ||
          Has(Permission::PERMISSION_VIEW, std::nullopt);
      if (everything_visible) {
        sql += " ORDER BY lower(display_name) ASC, id ASC";
        break;
      }
      std::string identity = "(is_group = ? AND lower(name) = ?)";
      params.emplace_back(false);
      params.emplace_back(util::AsciiLower(request_.session->user));
      if (!request_.session->groups.empty()) {
        std::string placeholders;
        for (size_t i = 0; i < request_.session->groups.size(); ++i) {
          placeholders += i == 0 ? "?" : ", ?";
        }
        identity += std::format(" OR (is_group = ? AND lower(name) IN ({}))",
                                placeholders);
        params.emplace_back(true);
        for (const std::string& group : request_.session->groups) {
          params.emplace_back(util::AsciiLower(group));
        }
      }
      sql += std::format(
          " ORDER BY EXISTS (SELECT 1 FROM permissions_product WHERE "
          "permissions_product.product_id = products.id AND "
          "permissions_product.permission IN ('PRODUCT_VIEW', "
          "'PRODUCT_ACCESS', 'PRODUCT_ADMIN', 'PRODUCT_STORE') AND ({})) "
          "DESC, lower(display_name) ASC, id ASC",
          identity);
    }
  }
  // 0 stands for a limit or offset that the client did not send. SQLite
  // needs a LIMIT before an OFFSET; -1 is no limit.
  if (limit > 0) {
    sql += std::format(" LIMIT {}", limit);
  } else if (offset > 0 &&
             config_db().database().dialect() == db::Dialect::kSqlite) {
    sql += " LIMIT -1";
  }
  if (offset > 0) sql += std::format(" OFFSET {}", offset);
  const db::QueryResult rows = ValueOrThrow(config_db().Query(sql, params));
  for (const db::Row& row : rows.rows) {
    result.push_back(ToApiProduct(ToProductRow(row)));
  }
}

void ProductHandler::getCurrentProduct(::Product& result) {
  if (request_.product == nullptr) {
    throw LoggedFailure(
        ErrorCode::IOERROR,
        "Requested current product from ProductService but the request came "
        "through the main endpoint.");
  }
  const std::optional<ProductRow> row = LoadRow(request_.product->id());
  if (!row) {
    throw LoggedFailure(
        ErrorCode::IOERROR,
        "The product requested has been disconnected from the server.");
  }
  result = ToApiProduct(*row);
}

int64_t ProductHandler::getProductCount(const std::string& endpoint_filter,
                                        const std::string& name_filter) {
  std::vector<db::Param> params;
  const std::string sql =
      "SELECT count(*) FROM products WHERE 1 = 1" +
      ProductFilters(endpoint_filter, name_filter, params);
  return ValueOrThrow(config_db().Query(sql, params)).rows.front().Int(0);
}

void ProductHandler::getProductConfiguration(ProductConfiguration& result,
                                             int64_t product_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW}, product_id);
  const ProductRow row = RequireRow(product_id);
  const db::ConnectionUrl url =
      db::ParseConnectionUrl(row.connection).value_or(db::ConnectionUrl{});
  DatabaseConnection connection;
  if (url.driver.find("postgresql") != std::string::npos) {
    connection.__set_engine("postgresql");
    connection.__set_host(url.host.value_or(""));
    connection.__set_port(url.port.value_or(0));
    connection.__set_username_b64(
        util::Base64Encode(url.username.value_or("")));
    connection.__set_database(url.database);
  } else {
    connection.__set_engine("sqlite");
    connection.__set_host("");
    connection.__set_port(0);
    connection.__set_username_b64(util::Base64Encode(""));
    connection.__set_database(
        WithoutWorkspace(url.database, request_.server->workspace()));
  }
  result.__set_id(row.id);
  result.__set_endpoint(row.endpoint);
  result.__set_displayedName_b64(util::Base64Encode(row.display_name));
  if (row.description && !row.description->empty()) {
    result.__set_description_b64(util::Base64Encode(*row.description));
  }
  result.__set_connection(connection);
  if (row.run_limit) result.__set_runLimit(*row.run_limit);
  result.__set_reportLimit(row.report_limit);
  if (row.is_review_status_change_disabled) {
    result.__set_isReviewStatusChangeDisabled(
        *row.is_review_status_change_disabled);
  }
  result.__set_confidentiality(ConfidentialityOf(row.confidentiality));
}

bool ProductHandler::CreateProductDatabase(
    const DatabaseConnection& connection) {
  if (connection.engine == "sqlite") {
    kLog.Info("Using SQLite engine, skipping database creation");
    return true;
  }
  Result<std::unique_ptr<db::Connection>> server = db::OpenPostgresql(
      {.host = connection.host,
       .port = connection.port,
       .user = DecodeBase64(connection.username_b64),
       .password = DecodeBase64(connection.password_b64),
       .database = "postgres"});
  if (!server.ok()) {
    kLog.Error(
        std::format("SQLAlchemyError occurred: {}", server.error().message));
    return false;
  }
  kLog.Info(std::format("Creating database '{}'", connection.database));
  Result<void> created = server.value()->Execute(std::format(
      "CREATE DATABASE \"{}\"",
      util::Replace(connection.database, "\"", "\"\"")));
  if (!created.ok()) {
    kLog.Error(std::format("ProgrammingError occurred: {}",
                           created.error().message));
    if (created.error().message.find("already exists") != std::string::npos) {
      kLog.Error(std::format("Database '{}' already exists",
                             connection.database));
    } else {
      kLog.Error(std::format("Error occurred while creating database: {}",
                             created.error().message));
    }
    return false;
  }
  return true;
}

std::string ProductHandler::ConnectionString(
    const DatabaseConnection& connection,
    const std::optional<std::string>& old_connection) {
  if (connection.engine == "sqlite") {
    // The path stays relative to the workspace.
    return db::Database::Sqlite(connection.database).ConnectionString();
  }
  std::string user = "codechecker";
  if (!connection.username_b64.empty()) {
    user = DecodeBase64(connection.username_b64);
  }
  std::optional<std::string> password;
  if (!connection.password_b64.empty()) {
    password = DecodeBase64(connection.password_b64);
  } else if (old_connection) {
    // The password was not changed: keep the old one, if there was one.
    password = db::ParseConnectionUrl(*old_connection)
                   .value_or(db::ConnectionUrl{})
                   .password;
  } else {
    password = "";
  }
  return db::Database::Postgresql({.host = connection.host,
                                   .port = connection.port,
                                   .user = user,
                                   .password = password,
                                   .database = connection.database})
      .ConnectionString();
}

bool ProductHandler::addProduct(const ProductConfiguration& product) {
  Require({Permission::SUPERUSER}, std::nullopt);
  kLog.Info(std::format("User requested add product '{}'", product.endpoint));
  if (!http::IsValidProductEndpoint(product.endpoint)) {
    throw LoggedFailure(ErrorCode::GENERAL,
                        "The specified endpoint is invalid.");
  }
  if (!product.__isset.connection) {
    throw LoggedFailure(
        ErrorCode::GENERAL,
        "Product cannot be added without a database configuration!");
  }
  const DatabaseConnection& connection = product.connection;
  if (connection.engine == "postgresql" &&
      !IsValidPostgresqlDatabaseName(connection.database)) {
    throw LoggedFailure(
        ErrorCode::DATABASE,
        std::format("The specified PostgreSQL database name '{}' contains "
                    "characters that are not allowed (quotes, semicolons, "
                    "whitespace, or control characters), or is empty, or "
                    "exceeds PostgreSQL's 63-byte identifier limit.",
                    connection.database));
  }
  ProductRegistry& registry = request_.server->products();
  if (registry.Get(product.endpoint) != nullptr) {
    throw LoggedFailure(
        ErrorCode::GENERAL,
        std::format("A product endpoint '/{}' is already configured!",
                    product.endpoint));
  }
  if (connection.engine == "sqlite" &&
      !util::pypath::Dirname(connection.database).empty()) {
    throw MakeRequestFailed(ErrorCode::DATABASE,
                            "SQLite database must be given by filename only!");
  }
  const bool sqlite = connection.engine == "sqlite";
  const bool used = ValueOrThrow(registry.IsDatabaseUsed(
      {.driver = connection.engine + (sqlite ? "+pysqlite" : "+psycopg2"),
       .host = connection.host,
       .port = connection.port,
       .database = sqlite && !connection.database.starts_with('/')
                       ? util::pypath::Join({request_.server->workspace(),
                                             connection.database})
                       : connection.database}));
  if (used) {
    kLog.Error(std::format("Database '{}' is already in use by another "
                           "product!",
                           connection.database));
    throw MakeRequestFailed(ErrorCode::DATABASE,
                            "Database is already in use by another product!");
  }
  if (CreateProductDatabase(connection)) {
    kLog.Info(std::format("Database '{}' created successfully.",
                          connection.database));
  }
  const std::string display_name = DisplayName(product);
  const db::Param description = Description(product);
  RequireKnownEngine(connection);
  const std::string connection_string =
      ConnectionString(connection, std::nullopt);

  kLog.Debug("Attempting database connection to new product...");
  ConnectProduct(registry,
                 {.id = 0,
                  .endpoint = product.endpoint,
                  .display_name = display_name,
                  .connection = connection_string},
                 /*init=*/true, connection, "add", product.endpoint);
  kLog.Debug("Product database successfully connected to.");
  db::LazyConnection& config = config_db();
  ValueOrThrow(config.Query(
      "INSERT INTO products (endpoint, connection, display_name, description, "
      "run_limit, report_limit, is_review_status_change_disabled, "
      "confidentiality) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
      {product.endpoint, connection_string, display_name, description,
       OptionalInt(product.__isset.runLimit, product.runLimit),
       // A missing limit gets the column's default, as SQLAlchemy does.
       product.__isset.reportLimit ? product.reportLimit : int64_t{500000},
       product.__isset.isReviewStatusChangeDisabled &&
           product.isReviewStatusChangeDisabled,
       ConfidentialityName(product)}));
  const int64_t id =
      ValueOrThrow(config.Query("SELECT id FROM products WHERE endpoint = ?",
                                {product.endpoint}))
          .rows.front()
          .Int(0);
  ThrowIfFailed(auth::InitialiseDefaults(
      auth::Scope::kProduct,
      {.config_db = ValueOrThrow(config.Get()), .product_id = id}));
  kLog.Debug("Product configuration added to database successfully.");

  // Connect again with the record that has the id.
  (void)registry.Remove(product.endpoint);
  registry.Add({.id = id,
                .endpoint = product.endpoint,
                .display_name = display_name,
                .connection = connection_string},
               /*init=*/false, /*count_runs=*/false);
  kLog.Debug("Product database connected and ready to serve.");
  return true;
}

bool ProductHandler::editProduct(int64_t product_id,
                                 const ProductConfiguration& new_config) {
  const ProductRow product = RequireRow(product_id);
  Require({Permission::PRODUCT_ADMIN}, product.id);
  kLog.Info(std::format("User requested edit product '{}'", product.endpoint));
  if (!new_config.__isset.connection) {
    throw LoggedFailure(ErrorCode::GENERAL,
                        "Product's database configuration cannot be removed!");
  }
  const DatabaseConnection& connection = new_config.connection;
  ProductRegistry& registry = request_.server->products();
  if (new_config.endpoint != product.endpoint) {
    if (!http::IsValidProductEndpoint(new_config.endpoint)) {
      throw LoggedFailure(ErrorCode::GENERAL,
                          "The endpoint to move the product to is invalid.");
    }
    if (registry.Get(new_config.endpoint) != nullptr) {
      // Python names the old endpoint here.
      throw LoggedFailure(
          ErrorCode::GENERAL,
          std::format("A product endpoint '/{}' is already configured!",
                      product.endpoint));
    }
    kLog.Info(std::format("User renamed product '{}' to '{}'",
                          product.endpoint, new_config.endpoint));
  }
  if (connection.engine == "sqlite") {
    // Old products may have an absolute path stored: compare it the way
    // getProductConfiguration() shows it.
    const std::string old_path = WithoutWorkspace(
        db::ParseConnectionUrl(product.connection)
            .value_or(db::ConnectionUrl{})
            .database,
        request_.server->workspace());
    if (connection.database != old_path &&
        !util::pypath::Dirname(connection.database).empty()) {
      throw MakeRequestFailed(
          ErrorCode::DATABASE,
          "SQLite database must be given by filename only!");
    }
  }
  const std::string display_name = DisplayName(new_config);
  const db::Param description = Description(new_config);
  RequireKnownEngine(connection);
  const std::string connection_string =
      ConnectionString(connection, product.connection);
  const std::string& workspace = request_.server->workspace();
  const bool reconnect =
      product.endpoint != new_config.endpoint ||
      ResolvedConnectionString(product.connection, workspace) !=
          ResolvedConnectionString(connection_string, workspace);
  if (reconnect) {
    // Changing the connection is only for super users.
    Require({Permission::SUPERUSER}, std::nullopt);
    // Try the new settings on a product with a temporary endpoint.
    std::string sample = new_config.endpoint;
    std::ranges::shuffle(sample, std::mt19937(std::random_device()()));
    const std::string dummy_endpoint =
        new_config.endpoint + "_" +
        sample.substr(0, std::min<size_t>(sample.size(), 5));
    kLog.Debug("Attempting database connection with new settings...");
    ConnectProduct(registry,
                   {.id = 0,
                    .endpoint = dummy_endpoint,
                    .display_name = display_name,
                    .connection = connection_string},
                   /*init=*/false, connection, "edit", new_config.endpoint);
    kLog.Debug("Product database successfully connected to.");
    (void)registry.Remove(dummy_endpoint);
  }
  ValueOrThrow(config_db().Query(
      "UPDATE products SET endpoint = ?, run_limit = ?, report_limit = ?, "
      "is_review_status_change_disabled = ?, connection = ?, "
      "display_name = ?, description = ?, confidentiality = ? WHERE id = ?",
      {new_config.endpoint,
       OptionalInt(new_config.__isset.runLimit, new_config.runLimit),
       OptionalInt(new_config.__isset.reportLimit, new_config.reportLimit),
       new_config.__isset.isReviewStatusChangeDisabled
           ? db::Param(new_config.isReviewStatusChangeDisabled)
           : db::Param(nullptr),
       connection_string, display_name, description,
       ConfidentialityName(new_config), product_id}));
  kLog.Info("Product configuration edited and saved successfully.");
  if (reconnect) {
    kLog.Info("Product change requires database reconnection...");
    kLog.Debug("Disconnecting...");
    // Another process may have the old endpoint, this one may not.
    (void)registry.Remove(product.endpoint);
    kLog.Debug("Connecting new settings...");
    registry.Add({.id = product_id,
                  .endpoint = new_config.endpoint,
                  .display_name = display_name,
                  .connection = connection_string},
                 /*init=*/false, /*count_runs=*/false);
    kLog.Info("Product reconnected successfully.");
  }
  return true;
}

bool ProductHandler::removeProduct(int64_t product_id) {
  Require({Permission::SUPERUSER}, std::nullopt);
  const ProductRow product = RequireRow(product_id);
  kLog.Info(std::format("User requested to remove product '{}'",
                        product.endpoint));
  ThrowIfFailed(request_.server->products().Remove(product.endpoint),
                ErrorCode::GENERAL);
  ValueOrThrow(
      config_db().Query("DELETE FROM products WHERE id = ?", {product_id}));
  return true;
}

}  // namespace cc::server::api
