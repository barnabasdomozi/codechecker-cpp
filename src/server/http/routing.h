// Port of codechecker_server/routing.py: which URLs name a product, an API
// version and a service (server-api.md §2.2, §2.3).

#ifndef CC_SERVER_HTTP_ROUTING_H_
#define CC_SERVER_HTTP_ROUTING_H_

#include <optional>
#include <string>
#include <string_view>

namespace cc::server::http {

// Returns whether `part` of a URL can be a product endpoint: letters,
// digits, '_' and '-', and none of the server's own paths (Python:
// is_valid_product_endpoint()).
bool IsValidProductEndpoint(std::string_view part);

// Returns whether the server supports the API version `version`
// ("v6.74", "6.74", ...): the same major version and a minor version not
// newer than the server's (Python: is_supported_version()).
bool IsSupportedVersion(std::string_view version);

// The parts of the path of an API call (Python:
// split_client_POST_request()).
struct ApiRoute {
  std::optional<std::string> product;  // nullopt: a server-wide call.
  std::string service;                 // For example "Products".
};

// Returns the parts of the request path `path`; nullopt if it is not an
// API path or its version is not supported.
std::optional<ApiRoute> SplitApiPath(std::string_view path);

// Returns the product endpoint that `path` of a GET request starts with;
// nullopt if it does not start with one (Python:
// split_client_GET_request()).
std::optional<std::string> ProductOfPagePath(std::string_view path);

// Returns `path` without its query and fragment (Python:
// urlparse(path).path).
std::string_view UrlPath(std::string_view path);

}  // namespace cc::server::http

#endif  // CC_SERVER_HTTP_ROUTING_H_
