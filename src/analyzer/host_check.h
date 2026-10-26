// Port of codechecker_analyzer/host_check.py: checks of the analyzer
// binaries and their features.

#ifndef CC_ANALYZER_HOST_CHECK_H_
#define CC_ANALYZER_HOST_CHECK_H_

#include <string>
#include <string_view>
#include <vector>

namespace cc::analyzer {

// Returns whether `binary --version` runs successfully (Python:
// check_analyzer()). Failures are logged.
bool CheckAnalyzer(const std::string& binary);

// Returns whether `clang -cc1 -analyzer-config-help` lists something that
// the regular expression `option_name` finds (Python:
// has_analyzer_config_option()).
bool HasAnalyzerConfigOption(const std::string& clang,
                             std::string_view option_name);

// Returns whether Clang analyzes a small C file with the extra flags
// `feature` (Python: has_analyzer_option()).
bool HasAnalyzerOption(const std::string& clang,
                       const std::vector<std::string>& feature);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_HOST_CHECK_H_
