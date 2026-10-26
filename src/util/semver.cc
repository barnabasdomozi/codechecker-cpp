#include "util/semver.h"

#include <stddef.h>
#include <stdint.h>

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/pyre.h"
#include "util/strings.h"

namespace cc::util {
namespace {

// Python: the comparison of one prerelease part; numbers are smaller than
// words.
std::strong_ordering ComparePart(const std::string& a, const std::string& b) {
  const std::optional<int64_t> number_a =
      a.find_first_not_of("0123456789") == std::string::npos ? ParseInt(a)
                                                             : std::nullopt;
  const std::optional<int64_t> number_b =
      b.find_first_not_of("0123456789") == std::string::npos ? ParseInt(b)
                                                             : std::nullopt;
  if (number_a && number_b) return *number_a <=> *number_b;
  if (number_a) return std::strong_ordering::less;
  if (number_b) return std::strong_ordering::greater;
  return a <=> b;
}

}  // namespace

std::string SemVer::ToString() const {
  std::string text = std::to_string(major) + "." + std::to_string(minor) +
                     "." + std::to_string(patch);
  if (!prerelease.empty()) text += "-" + prerelease;
  if (!build.empty()) text += "+" + build;
  return text;
}

std::optional<SemVer> ParseSemVer(std::string_view text) {
  static const Regex& kVersion = *new Regex(
      R"(^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*))"
      R"((?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))"
      R"((?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?)"
      R"((?:\+(?P<build>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)");
  const std::optional<RegexMatch> match = kVersion.Match(text);
  if (!match) return std::nullopt;
  const auto number = [&match](std::string_view group) {
    return static_cast<int>(ParseInt(*match->Group(group)).value_or(0));
  };
  return SemVer{
      .major = number("major"),
      .minor = number("minor"),
      .patch = number("patch"),
      .prerelease = std::string(match->Group("prerelease").value_or("")),
      .build = std::string(match->Group("build").value_or(""))};
}

std::strong_ordering operator<=>(const SemVer& a, const SemVer& b) {
  if (const auto order = a.major <=> b.major; order != 0) return order;
  if (const auto order = a.minor <=> b.minor; order != 0) return order;
  if (const auto order = a.patch <=> b.patch; order != 0) return order;
  // A version with a prerelease comes before the release.
  if (a.prerelease.empty() || b.prerelease.empty()) {
    return a.prerelease.empty() <=> b.prerelease.empty();
  }
  const std::vector<std::string> parts_a = Split(a.prerelease, ".");
  const std::vector<std::string> parts_b = Split(b.prerelease, ".");
  for (size_t i = 0; i < parts_a.size() && i < parts_b.size(); ++i) {
    if (const auto order = ComparePart(parts_a[i], parts_b[i]); order != 0) {
      return order;
    }
  }
  return parts_a.size() <=> parts_b.size();
}

bool operator==(const SemVer& a, const SemVer& b) { return (a <=> b) == 0; }

}  // namespace cc::util
