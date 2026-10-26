// Port of codechecker_analyzer/makefile.py: a Makefile that runs the
// analysis instead of `analyze` (makefile-mode.md).

#ifndef CC_ANALYZER_ANALYSIS_MAKEFILE_H_
#define CC_ANALYZER_ANALYSIS_MAKEFILE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "analyzer/analysis/analysis_manager.h"
#include "analyzer/analysis/pre_analysis_manager.h"
#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/analyzer.h"

namespace cc::analyzer {

// Writes `output_path`/Makefile with targets for `actions` (Python:
// MakeFileCreator.create()). With `pre_analysis`, the Clang SA actions get
// CTU and statistics collect targets first.
void CreateMakefile(const std::vector<std::string>& analyzers,
                    const std::string& output_path,
                    const std::map<std::string, AnalyzerConfig>& configs,
                    bool pre_analysis,
                    const std::optional<StatisticsData>& statistics,
                    const std::optional<CtuData>& ctu_data,
                    const std::vector<BuildAction>& actions);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_ANALYSIS_MAKEFILE_H_
