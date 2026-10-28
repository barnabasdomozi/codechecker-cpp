#include "common/util.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/subprocess.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/yaml.h"

namespace cc::common {
namespace {

constexpr util::Logger kLog("system");

}  // namespace

std::optional<util::JsonDocument> LoadJson(const std::string& path,
                                           bool display_warning) {
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    if (display_warning) {
      kLog.Warning(std::format("Failed to open json file: {}", path));
      kLog.Warning(content.error().message);
    }
    return std::nullopt;
  }
  Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(content.value()));
  if (!document.ok()) {
    if (display_warning) {
      kLog.Warning(std::format("{} is not a valid json file.", path));
      kLog.Warning(document.error().message);
    }
    return std::nullopt;
  }
  return std::move(document).value();
}

std::optional<util::JsonDocument> LoadYaml(const std::string& path) {
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    kLog.Warning(std::format("Failed to open YAML file: {}", path));
    kLog.Warning(content.error().message);
    return std::nullopt;
  }
  Result<util::JsonDocument> document =
      util::SafeLoadYaml(content.value(), path);
  if (!document.ok()) {
    kLog.Warning(std::format("Failed to parse YAML file: {}", path));
    kLog.Warning(document.error().message);
    return std::nullopt;
  }
  return std::move(document).value();
}

Result<std::vector<std::string>> LoadArgsFromFile(const std::string& path) {
  using Args = Result<std::vector<std::string>>;
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    if (!util::pypath::Exists(path)) {
      return Args(Error{.message = "File not found: " + path});
    }
    return Args(content.error());
  }
  const std::string text(util::Strip(
      util::TranslateNewlines(util::DecodeUtf8Ignore(content.value()))));
  // Python: re.sub(r'\$\((.*?)\)', ...).
  static const util::Regex& kVariable = *new util::Regex(R"(\$\((.*?)\))");
  std::string expanded;
  size_t pos = 0;
  for (const util::RegexMatch& match : kVariable.FindAll(text)) {
    expanded.append(text, pos, match.Start() - pos);
    const std::string name(*match.Group(1));
    if (const std::optional<std::string> value = platform::GetEnv(name)) {
      expanded += *value;
    } else {
      kLog.Error(std::format("{} environment variable not set in {}", name,
                             path));
    }
    pos = match.End();
  }
  expanded.append(text, pos);
  return util::ShlexSplit(expanded);
}

std::string PathForFakeRoot(const std::string& full_path,
                            const std::string& root_path) {
  return util::pypath::Abspath(util::pypath::Join(
      {root_path, util::pypath::Relpath(full_path, "/")}));
}

std::string FormatSize(double bytes) {
  constexpr std::string_view kUnits[] = {"",   "Ki", "Mi", "Gi", "Ti",
                                         "Pi", "Ei", "Zi", "Yi", "Ri"};
  for (const std::string_view unit : kUnits) {
    if (std::abs(bytes) < 1024.0) {
      return std::format("{:3.1f} {}B", bytes, unit);
    }
    bytes /= 1024.0;
  }
  return std::format("{:.1f} QiB", bytes);
}

bool StrToBool(std::string_view value) {
  constexpr std::string_view kTrue[] = {"y", "yes", "t", "true", "on", "1"};
  return std::ranges::find(kTrue, util::AsciiLower(value)) != std::end(kTrue);
}

}  // namespace cc::common
