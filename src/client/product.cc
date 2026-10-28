#include "client/product.h"

#include <stddef.h>
#include <stdint.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/strings.h"

namespace cc::client {
namespace {

// A host and the port given with it, if any.
struct ServerAddress {
  std::string host;
  std::optional<int64_t> port = std::nullopt;
};

// Returns the host and port of "host", "host:port", "[ipv6]" or
// "[ipv6]:port" (Python: understand_server_addr()); nullopt if the address
// has another form or the port is not a number.
std::optional<ServerAddress> UnderstandServerAddress(std::string_view address) {
  const std::vector<std::string> parts = util::Split(address, ":");
  if (parts.size() == 1) return ServerAddress{.host = parts[0]};
  // Python: int(port), which raises ValueError for anything else.
  const auto with_port = [](std::string host, const std::string& port)
      -> std::optional<ServerAddress> {
    const std::optional<int64_t> number = util::ParseInt(port);
    if (!number) return std::nullopt;
    return ServerAddress{.host = std::move(host), .port = number};
  };
  if (parts.size() == 2) return with_port(parts[0], parts[1]);
  // Several colons: an IPv6 address in brackets, maybe with a port.
  if (parts.front().starts_with('[')) {
    if (parts.back().ends_with(']')) {
      return ServerAddress{.host = std::string(address)};
    }
    if (parts[parts.size() - 2].ends_with(']')) {
      const std::vector<std::string> host(parts.begin(), parts.end() - 1);
      return with_port(util::Join(host, ":"), parts.back());
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<ProductUrl> SplitProductUrl(std::string_view url) {
  ProductUrl result{.protocol = "http", .host = "localhost", .port = 8001};
  std::string rest(url);
  // Python: __strip_protocol_from_url() and
  // expand_whole_protocol_and_port().
  if (const size_t separator = rest.find("://");
      separator != std::string::npos) {
    result.protocol = rest.substr(0, separator);
    if (result.protocol == "http") {
      result.port = 80;
    } else if (result.protocol == "https") {
      result.port = 443;
    } else {
      return std::nullopt;
    }
    // Python replaces every occurrence of "<protocol>://".
    rest = util::Replace(rest, result.protocol + "://", "");
    rest = std::string(util::LeftStrip(rest, "/"));
    rest.erase(rest.find_last_not_of('/') + 1);
  }
  const std::vector<std::string> parts = util::Split(rest, "/");
  if (parts.size() == 1) {
    // One word is a product name on localhost, but a host name such as
    // "example.com:1234" is not taken as one.
    result.product = parts[0];
    if (result.product.empty() ||
        (result.product[0] >= '0' && result.product[0] <= '9') ||
        result.product.find_first_of(".:") != std::string::npos) {
      return std::nullopt;
    }
  } else if (parts.size() == 2) {
    result.product = parts[1];
    std::optional<ServerAddress> address = UnderstandServerAddress(parts[0]);
    if (!address) return std::nullopt;
    result.host = std::move(address->host);
    // Python: `if maybe_port:`, so port 0 keeps the default.
    if (address->port && *address->port != 0) result.port = *address->port;
  } else {
    return std::nullopt;
  }
  return result;
}

std::string CreateProductUrl(std::string_view protocol, std::string_view host,
                             int64_t port, std::string_view path) {
  return std::format("{}://{}:{}{}", protocol, host, port, path);
}

}  // namespace cc::client
