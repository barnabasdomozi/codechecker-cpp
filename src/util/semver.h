// Port of the parts of Python's semver.Version that the analyzer checks use
// (libraries.md, "Small Python modules").

#ifndef CC_UTIL_SEMVER_H_
#define CC_UTIL_SEMVER_H_

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace cc::util {

// A semantic version: major.minor.patch[-prerelease][+build].
struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string prerelease = {};
  std::string build = {};

  // Returns the version as str(Version) writes it, for example "19.1.7".
  std::string ToString() const;
};

// Parses `text` like Version.parse(text); nullopt where it raises
// ValueError.
std::optional<SemVer> ParseSemVer(std::string_view text);

// Orders versions by precedence, ignoring the build part (Python:
// Version.compare()).
std::strong_ordering operator<=>(const SemVer& a, const SemVer& b);
bool operator==(const SemVer& a, const SemVer& b);

}  // namespace cc::util

#endif  // CC_UTIL_SEMVER_H_
