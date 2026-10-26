// Port of codechecker_analyzer/analyzers/analyzer_types.py: which analyzers
// can run, what Clang can do, and the configurations of an analysis
// (analyzers.md §3).

#ifndef CC_ANALYZER_DRIVERS_ANALYZER_TYPES_H_
#define CC_ANALYZER_DRIVERS_ANALYZER_TYPES_H_

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/drivers/analyzer.h"
#include "util/result.h"

namespace cc::analyzer {

// The analyzers that can run, and why the others cannot.
struct AnalyzerAvailability {
  std::vector<std::string> enabled;
  std::vector<std::pair<std::string, std::string>> failed;  // Name, reason.
};

// Checks whether the analyzers `names` can run (Python:
// check_supported_analyzers()). A configured binary without a folder may be
// replaced by one found in PATH.
AnalyzerAvailability CheckSupportedAnalyzers(
    const std::vector<std::string>& names);

// Python: check_available_analyzers(): with `names`, an analyzer that
// cannot run is an error; without, all analyzers are checked and it is an
// error if none can run. Errors are logged and have exit code 1.
Result<AnalyzerAvailability> CheckAvailableAnalyzers(
    const std::optional<std::vector<std::string>>& names);

// Logs a warning for each analyzer that cannot run (Python:
// print_unsupported_analyzers()).
void PrintUnsupportedAnalyzers(
    const std::vector<std::pair<std::string, std::string>>& failed);

// Python: is_statistics_capable(), is_z3_capable(),
// is_z3_refutation_capable().
bool IsStatisticsCapable();
bool IsZ3Capable();
bool IsZ3RefutationCapable();

// Returns the configurations of `analyzers` for an analysis (Python:
// build_config_handlers()).
Result<std::map<std::string, AnalyzerConfig>> BuildConfigs(
    const ConfigArgs& args, const std::vector<std::string>& analyzers);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_ANALYZER_TYPES_H_
