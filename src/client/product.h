// Port of codechecker_client/product.py: the product URLs of the client
// commands (server-api.md §3).

#ifndef CC_CLIENT_PRODUCT_H_
#define CC_CLIENT_PRODUCT_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

namespace cc::client {

// The parts of a product URL, "[http[s]://]host[:port]/Product".
struct ProductUrl {
  std::string protocol = {};  // "http" or "https".
  std::string host = {};      // An IPv6 address keeps its brackets.
  int64_t port = 0;
  std::string product = {};
};

// Returns the parts of the product URL `url` (Python: split_product_url()):
// without a protocol, "http" and port 8001; without a host, "localhost".
// nullopt if `url` is malformed.
std::optional<ProductUrl> SplitProductUrl(std::string_view url);

// Returns the URL of `path` on the server at `host`:`port` (Python:
// create_product_url()).
std::string CreateProductUrl(std::string_view protocol, std::string_view host,
                             int64_t port, std::string_view path);

}  // namespace cc::client

#endif  // CC_CLIENT_PRODUCT_H_
