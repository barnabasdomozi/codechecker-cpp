#include "client/cmd/products.h"

#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/codeCheckerProductService.h"
#include "api/thrift/products_types.h"
#include "client/client.h"
#include "client/cmd/cmd_line.h"
#include "client/cmd/cmd_line_client.h"
#include "client/product.h"
#include "util/argparse.h"
#include "util/base64.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"
#include "web/convert.h"
#include "web/database_status.h"

namespace cc::client::cmd {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");

// The limit sent to getProducts() for "no limit". Python sends none, but
// the generated client always sends one, and the Python server takes 0 as
// LIMIT 0. No server has this many products.
constexpr int64_t kNoLimit = std::numeric_limits<int64_t>::max();

// Returns the client of the server-wide Products service of the server at
// `server_url` (the start of Python's handlers). A malformed URL ends the
// command as Python's uncaught ValueError does: the last line of its
// traceback on stdout, and exit code 1 (known-issues #32).
Result<ProductClient> SetupServerProductClient(const std::string& server_url) {
  const Result<ProductUrl> url = SplitServerUrl(server_url);
  if (!url.ok()) {
    util::Print("ValueError: " + url.error().message);
    return Result<ProductClient>(Error{});
  }
  return SetupProductClient(url.value());
}

// Returns the products whose endpoint matches `endpoint_filter`, a pattern
// with '*' wildcards in any case, or every product for "" (Python:
// getProducts(endpoint_filter, None, None, None, None)).
Result<Products> GetProducts(ProductClient& client,
                             const std::string& endpoint_filter) {
  // Python sends no sort mode either; the server's default is ACCESSRIGHT.
  Products products;
  const Result<void> called = client.Call(
      "getProducts", [&](codeCheckerProductServiceClient& service) {
        service.getProducts(products, endpoint_filter,
                            /*productNameFilter=*/"", kNoLimit,
                            /*offset=*/0, ProductSortMode::ACCESSRIGHT);
      });
  if (!called.ok()) return Result<Products>(called.error());
  return Result<Products>(std::move(products));
}

// Returns the text in the Base64 `text` of the API, "" for "".
std::string FromApiBase64(const std::string& text) {
  // The servers always send valid Base64.
  return web::FromBase64(text).value_or(std::string());
}

// Prints `products` as a JSON list on one line, each product as an object
// with its endpoint as the only key.
void PrintProductsJson(const Products& products) {
  const util::MutableJsonDocument doc = util::NewJsonDocument();
  yyjson_mut_val* list = yyjson_mut_arr(doc.get());
  for (const Product& product : products) {
    yyjson_mut_val* item = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add(item, util::MakeJsonString(doc.get(), product.endpoint),
                       ToJson(doc.get(), product));
    yyjson_mut_arr_append(list, item);
  }
  util::Print(util::DumpJson(list));
}

// Returns the "Database status" column of `product` in the product table.
std::string_view DatabaseStatusText(const Product& product) {
  if (!product.accessible) return "No access.";
  if (!product.__isset.databaseStatus) return web::kUnknownDatabaseStatus;
  return web::DatabaseStatusMessage(product.databaseStatus);
}

// Prints `products` as a table in `format`: "plaintext", "rows", "table"
// or "csv".
void PrintProductsTable(std::string_view format, const Products& products) {
  std::vector<std::vector<std::string>> rows;
  for (const Product& product : products) {
    rows.push_back({std::string(DatabaseStatusText(product)), product.endpoint,
                    FromApiBase64(product.displayedName_b64),
                    FromApiBase64(product.description_b64)});
  }
  util::Print(util::twodim::ToString(
      format, {"Database status", "Endpoint", "Name", "Description"}, rows));
}

// Returns the connection to the database of the product that `args`
// describes: --postgresql and its options, or --sqlite.
DatabaseConnection DatabaseConnectionOf(const argparse::Namespace& args) {
  DatabaseConnection connection;
  std::string user;
  std::string password;
  if (args.Has("postgresql")) {
    connection.__set_engine("postgresql");
    connection.__set_host(args.Get("dbaddress"));
    // argparse checked that it is a number. Thrift sends it as an i32.
    connection.__set_port(
        static_cast<int32_t>(util::ParseInt(args.Get("dbport")).value_or(0)));
    connection.__set_database(args.Get("dbname"));
    user = args.Get("dbusername");
    password = args.Get("dbpassword");
  } else {
    connection.__set_engine("sqlite");
    connection.__set_host("");
    connection.__set_port(0);
    connection.__set_database(args.Get("sqlite"));
  }
  connection.__set_username_b64(util::Base64Encode(user));
  connection.__set_password_b64(util::Base64Encode(password));
  return connection;
}

// Returns the configuration of the product that `args` describes. The
// fields that are not set are not sent, as Python's None.
ProductConfiguration ProductConfigurationOf(const argparse::Namespace& args) {
  ProductConfiguration product;
  product.__set_endpoint(args.Get("endpoint"));
  if (args.Has("display_name")) {
    product.__set_displayedName_b64(
        util::Base64Encode(args.Get("display_name")));
  }
  if (args.Has("description")) {
    product.__set_description_b64(util::Base64Encode(args.Get("description")));
  }
  // Python: `if args.report_limit:`, so 0 sends no limit either.
  if (args.Has("report_limit")) {
    const int64_t report_limit =
        util::ParseInt(args.Get("report_limit")).value_or(0);
    if (report_limit != 0) product.__set_reportLimit(report_limit);
  }
  product.__set_connection(DatabaseConnectionOf(args));
  return product;
}

}  // namespace

Result<void> ListProducts(const argparse::Namespace& args) {
  SetupCmdLogger(args);
  Result<ProductClient> client =
      SetupServerProductClient(args.Get("server_url"));
  if (!client.ok()) return Result<void>(client.error());
  const Result<Products> products = GetProducts(client.value(), "");
  if (!products.ok()) return Result<void>(products.error());
  const std::string& format = args.Get("output_format");
  if (format == "json") {
    PrintProductsJson(products.value());
  } else {
    PrintProductsTable(format, products.value());
  }
  return Result<void>();
}

Result<void> AddProduct(const argparse::Namespace& args) {
  SetupCmdLogger(args);
  Result<ProductClient> client =
      SetupServerProductClient(args.Get("server_url"));
  if (!client.ok()) return Result<void>(client.error());
  const ProductConfiguration product = ProductConfigurationOf(args);
  kLog.Debug("Sending request to add product...");
  bool success = false;
  const Result<void> called = client.value().Call(
      "addProduct", [&](codeCheckerProductServiceClient& service) {
        success = service.addProduct(product);
      });
  if (!called.ok()) return called;
  if (!success) {
    kLog.Error("Adding the product has failed.");
    return Result<void>(Error{});
  }
  kLog.Info("Product added successfully.");
  return Result<void>();
}

Result<void> DelProduct(const argparse::Namespace& args) {
  SetupCmdLogger(args);
  Result<ProductClient> client =
      SetupServerProductClient(args.Get("server_url"));
  if (!client.ok()) return Result<void>(client.error());
  // The endpoint filter matches in any case, so the endpoint is compared
  // again.
  const std::string& endpoint = args.Get("endpoint");
  const Result<Products> products = GetProducts(client.value(), endpoint);
  if (!products.ok()) return Result<void>(products.error());
  const auto product =
      std::ranges::find(products.value(), endpoint, &Product::endpoint);
  if (product == products.value().end()) {
    kLog.Error(std::format("The product '{}' does not exist!", endpoint));
    return Result<void>();
  }
  bool success = false;
  const Result<void> called = client.value().Call(
      "removeProduct", [&](codeCheckerProductServiceClient& service) {
        success = service.removeProduct(product->id);
      });
  if (!called.ok()) return called;
  if (!success) {
    kLog.Error("An error occurred in product removal.");
    return Result<void>(Error{});
  }
  kLog.Info("Product removed.");
  return Result<void>();
}

}  // namespace cc::client::cmd
