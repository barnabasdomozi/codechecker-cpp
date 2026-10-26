// Port of codechecker_server/api/product_server.py: the Products service,
// which lists, adds, edits and removes the products of the server
// (cmd-products.md in docs/reference).

#ifndef CC_SERVER_API_PRODUCT_HANDLER_H_
#define CC_SERVER_API_PRODUCT_HANDLER_H_

#include <stdint.h>

#include <initializer_list>
#include <optional>
#include <string>

#include "api/thrift/codeCheckerProductService.h"
#include "api/thrift/products_types.h"
#include "api/thrift/shared_types.h"
#include "server/database/connection.h"
#include "server/server_context.h"

namespace cc::server::api {

// Answers the Products calls of one request. Errors are thrown as
// RequestFailed.
class ProductHandler final : public codeCheckerProductServiceIf {
 public:
  explicit ProductHandler(const Request& request) : request_(request) {}

  ProductHandler(const ProductHandler&) = delete;
  ProductHandler& operator=(const ProductHandler&) = delete;

  void getPackageVersion(std::string& result) override;
  bool isAdministratorOfAnyProduct() override;
  // `limit` and `offset` are 0 when the client did not send them, which
  // the port takes as Python's None.
  void getProducts(Products& result, const std::string& endpoint_filter,
                   const std::string& name_filter, int64_t limit,
                   int64_t offset, ProductSortMode::type sorting_mode) override;
  void getCurrentProduct(::Product& result) override;
  int64_t getProductCount(const std::string& endpoint_filter,
                          const std::string& name_filter) override;
  void getProductConfiguration(ProductConfiguration& result,
                               int64_t product_id) override;
  bool addProduct(const ProductConfiguration& product) override;
  bool editProduct(int64_t product_id,
                   const ProductConfiguration& new_config) override;
  bool removeProduct(int64_t product_id) override;

 private:
  // The columns of a products row that the calls use.
  struct ProductRow {
    int64_t id = 0;
    std::string endpoint;
    std::string connection;
    std::string display_name;
    std::optional<std::string> description;
    std::optional<int64_t> run_limit;
    std::optional<bool> is_review_status_change_disabled;
    std::optional<std::string> latest_storage_date;
    int64_t num_of_runs = 0;
    std::optional<std::string> confidentiality;
    int64_t report_limit = 0;
  };

  // Returns the row `row` of a "SELECT <kProductColumns> FROM products".
  static ProductRow ToProductRow(const db::Row& row);

  // Returns the row of the product `id`; nullopt if there is none.
  std::optional<ProductRow> LoadRow(int64_t id);

  // Returns the row of the product `id`; throws DATABASE "Product with ID
  // <id> does not exist!" if there is none.
  ProductRow RequireRow(int64_t id);

  // Returns whether the caller has `permission` at `product_id` (Python:
  // permissions.require_permission()).
  bool Has(Permission::type permission, std::optional<int64_t> product_id);

  // Throws UNAUTHORIZED unless the caller has one of `permissions` (Python:
  // __require_permission()).
  void Require(std::initializer_list<Permission::type> permissions,
               std::optional<int64_t> product_id);

  // Returns the Product of the API for `row`, connecting to the product
  // if this process is not connected yet (Python: __get_product()).
  ::Product ToApiProduct(const ProductRow& row);

  // Returns the connection string of the database of `connection`, for
  // the product at `endpoint` (Python: the conn_str part of addProduct()
  // and editProduct()). `old_connection` is the product's old connection
  // string, whose password is kept if `connection` has none.
  std::string ConnectionString(
      const DatabaseConnection& connection,
      const std::optional<std::string>& old_connection);

  // Creates the PostgreSQL database of `connection`; returns whether it was
  // created (Python: __create_product_database()).
  bool CreateProductDatabase(const DatabaseConnection& connection);

  db::LazyConnection& config_db() { return request_.server->config_db(); }

  Request request_;
};

}  // namespace cc::server::api

#endif  // CC_SERVER_API_PRODUCT_HANDLER_H_
