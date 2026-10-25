// The version files of the package, config/analyzer_version.json and
// config/web_version.json (version.md in docs/reference).

#ifndef CC_COMMON_VERSION_H_
#define CC_COMMON_VERSION_H_

#include <optional>
#include <string>

#include "util/result.h"

namespace cc::common {

// The version information of one part of the package, the analyzer or the
// web part. Values missing from the version file are nullopt (Python: None).
struct VersionInfo {
  // "6.30.0", followed by "-rc<n>" for a release candidate.
  std::string version;
  std::string build_date;
  std::optional<std::string> git_hash;
  std::optional<std::string> git_tag;
  // The Thrift API version; only the web part has one.
  std::optional<std::string> api_version;
};

// Reads the version file at `path` (Python: __set_version() of the analyzer
// and web contexts). The git tag is the "dirty" one when the log level is
// DEBUG or DEBUG_ANALYZER, so call it after util::SetupLogger(). Fails with
// exit code 1 if the file cannot be read; the logged warnings say why.
Result<VersionInfo> ReadVersionFile(const std::string& path);

}  // namespace cc::common

#endif  // CC_COMMON_VERSION_H_
