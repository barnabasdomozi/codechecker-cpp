#include "common/util.h"

#include <format>
#include <optional>
#include <string>
#include <utility>

#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/unicode.h"

namespace cc::common {
namespace {

constexpr util::Logger kLog("system");

}  // namespace

std::optional<util::JsonDocument> LoadJson(const std::string& path) {
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    kLog.Warning(std::format("Failed to open json file: {}", path));
    kLog.Warning(content.error().message);
    return std::nullopt;
  }
  Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(content.value()));
  if (!document.ok()) {
    kLog.Warning(std::format("{} is not a valid json file.", path));
    kLog.Warning(document.error().message);
    return std::nullopt;
  }
  return std::move(document).value();
}

}  // namespace cc::common
