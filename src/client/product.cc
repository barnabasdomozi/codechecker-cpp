#include "client/product.h"

#include <stddef.h>
#include <stdint.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::client {
namespace {

constexpr util::Logger kLog("system");

// The parts of a URL before and after its protocol.
struct ProtocolAndRest {
  std::string protocol = {};  // "http" or "https".
  int64_t port = 0;           // The default port of the protocol.
  std::string rest = {};      // The URL after "<protocol>://".
};

// A host and the port given with it, if any.
struct ServerAddress {
  std::string host = {};
  std::optional<int64_t> port = std::nullopt;
};

// Returns the protocol of `url` with its default port, and the rest of
// `url`: without a protocol, "http" and port 8001; with one, the rest has
// no '/' at either end (Python: __strip_protocol_from_url() and
// expand_whole_protocol_and_port()). A protocol other than "http" and
// "https" fails with the message of Python's ValueError.
Result<ProtocolAndRest> StripProtocol(std::string_view url) {
  const size_t separator = url.find("://");
  if (separator == std::string_view::npos) {
    return Result<ProtocolAndRest>(ProtocolAndRest{
        .protocol = "http", .port = 8001, .rest = std::string(url)});
  }
  ProtocolAndRest result{.protocol = std::string(url.substr(0, separator))};
  if (result.protocol == "http") {
    result.port = 80;
  } else if (result.protocol == "https") {
    result.port = 443;
  } else {
    return Result<ProtocolAndRest>(Error{
        .message = std::format("'{}' is not a protocol understood by "
                               "CodeChecker",
                               result.protocol)});
  }
  // Python replaces every occurrence of "<protocol>://".
  std::string rest = util::Replace(url, result.protocol + "://", "");
  rest = std::string(util::LeftStrip(rest, "/"));
  rest.erase(rest.find_last_not_of('/') + 1);
  result.rest = std::move(rest);
  return Result<ProtocolAndRest>(std::move(result));
}

// Returns the host and port of "host", "host:port", "[ipv6]" or
// "[ipv6]:port" (Python: understand_server_addr()). Another form, or a port
// that is not a number, fails with the message of Python's ValueError.
Result<ServerAddress> UnderstandServerAddress(std::string_view address) {
  const std::vector<std::string> parts = util::Split(address, ":");
  if (parts.size() == 1) {
    return Result<ServerAddress>(ServerAddress{.host = parts[0]});
  }
  // Python: int(port).
  const auto with_port = [](std::string host, const std::string& port) {
    const std::optional<int64_t> number = util::ParseInt(port);
    if (!number) return Result<ServerAddress>(util::IntValueError(port));
    return Result<ServerAddress>(
        ServerAddress{.host = std::move(host), .port = number});
  };
  if (parts.size() == 2) return with_port(parts[0], parts[1]);
  // Several colons: an IPv6 address in brackets, maybe with a port.
  if (parts.front().starts_with('[')) {
    if (parts.back().ends_with(']')) {
      return Result<ServerAddress>(
          ServerAddress{.host = std::string(address)});
    }
    if (parts[parts.size() - 2].ends_with(']')) {
      const std::vector<std::string> host(parts.begin(), parts.end() - 1);
      return with_port(util::Join(host, ":"), parts.back());
    }
  }
  return Result<ServerAddress>(
      Error{.message = "The server's address is not in a valid 'host:port' "
                       "or '[host]:port' format!"});
}

// Sets the host of `address` and its port, if it has one, in `url`.
void SetServerAddress(ServerAddress address, ProductUrl& url) {
  url.host = std::move(address.host);
  // Python: `if maybe_port:`, so port 0 keeps the default.
  if (address.port && *address.port != 0) url.port = *address.port;
}

}  // namespace

std::optional<ProductUrl> SplitProductUrl(std::string_view url) {
  kLog.Debug(std::format("Parsing product url '{}'", url));
  Result<ProtocolAndRest> stripped = StripProtocol(url);
  if (!stripped.ok()) return std::nullopt;
  ProductUrl result{.protocol = std::move(stripped.value().protocol),
                    .host = "localhost",
                    .port = stripped.value().port};
  const std::vector<std::string> parts =
      util::Split(stripped.value().rest, "/");
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
    Result<ServerAddress> address = UnderstandServerAddress(parts[0]);
    if (!address.ok()) return std::nullopt;
    SetServerAddress(std::move(address).value(), result);
  } else {
    return std::nullopt;
  }
  kLog.Debug(std::format("Result: With '{}' on server '{}:{}', product '{}'",
                         result.protocol, result.host, result.port,
                         result.product));
  return result;
}

Result<ProductUrl> SplitServerUrl(std::string_view url) {
  kLog.Debug(std::format("Parsing server url '{}'", url));
  Result<ProtocolAndRest> stripped = StripProtocol(url);
  if (!stripped.ok()) return Result<ProductUrl>(stripped.error());
  // Only the part before the first '/' is used.
  const std::string_view rest = stripped.value().rest;
  Result<ServerAddress> address =
      UnderstandServerAddress(rest.substr(0, rest.find('/')));
  if (!address.ok()) return Result<ProductUrl>(address.error());
  ProductUrl result{.protocol = std::move(stripped.value().protocol),
                    .port = stripped.value().port};
  SetServerAddress(std::move(address).value(), result);
  kLog.Debug(std::format("Result: With '{}' on server '{}:{}'",
                         result.protocol, result.host, result.port));
  return Result<ProductUrl>(std::move(result));
}

std::string CreateProductUrl(std::string_view protocol, std::string_view host,
                             int64_t port, std::string_view path) {
  return std::format("{}://{}:{}{}", protocol, host, port, path);
}

}  // namespace cc::client
