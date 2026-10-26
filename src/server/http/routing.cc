#include "server/http/routing.h"

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/api_version.h"
#include "util/strings.h"

namespace cc::server::http {
namespace {

// Paths of the server that are not product endpoints (Python:
// NON_PRODUCT_ENDPOINTS).
constexpr std::string_view kNonProductEndpoints[] = {
    "index.html", "images",   "docs",
    "live",       "ready",    "Authentication",
    "Products",   "CodeCheckerService", "Tasks"};

// Returns `text` without its leading 'v' characters (Python: lstrip('v')).
std::string_view StripVersionPrefix(std::string_view text) {
  while (text.starts_with('v')) text.remove_prefix(1);
  return text;
}

}  // namespace

bool IsValidProductEndpoint(std::string_view part) {
  if (std::ranges::find(kNonProductEndpoints, part) !=
      std::end(kNonProductEndpoints)) {
    return false;
  }
  return !part.empty() && std::ranges::all_of(part, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}

bool IsSupportedVersion(std::string_view version) {
  const std::vector<std::string> parts =
      util::Split(StripVersionPrefix(version), ".");
  if (parts.size() < 2) return false;
  const std::vector<std::string> server = util::Split(api::kApiVersion, ".");
  // Python: int() of the parts.
  const std::optional<int64_t> major = util::ParseInt(parts[0]);
  const std::optional<int64_t> minor = util::ParseInt(parts[1]);
  return major && minor && *major == util::ParseInt(server[0]) &&
         *minor <= util::ParseInt(server[1]).value_or(0);
}

std::optional<ApiRoute> SplitApiPath(std::string_view path) {
  const std::vector<std::string> parts = util::Split(UrlPath(path), "/", 3);
  if (parts.size() < 2) return std::nullopt;
  if (IsValidProductEndpoint(parts[1]) && parts.size() == 4) {
    if (!IsSupportedVersion(parts[2])) return std::nullopt;
    return ApiRoute{.product = parts[1], .service = parts[3]};
  }
  if (parts[1].starts_with('v') && parts.size() == 3) {
    if (!IsSupportedVersion(parts[1])) return std::nullopt;
    return ApiRoute{.product = std::nullopt, .service = parts[2]};
  }
  return std::nullopt;
}

std::optional<std::string> ProductOfPagePath(std::string_view path) {
  const std::vector<std::string> parts = util::Split(UrlPath(path), "/", 2);
  if (parts.size() > 1 && IsValidProductEndpoint(parts[1])) return parts[1];
  return std::nullopt;
}

std::string_view UrlPath(std::string_view path) {
  return path.substr(0, path.find_first_of("?#"));
}

}  // namespace cc::server::http
