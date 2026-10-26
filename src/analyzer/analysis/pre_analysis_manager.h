// Port of codechecker_analyzer/pre_analysis_manager.py: the collect phases
// of CTU and statistical analysis (ctu-analysis.md §4,
// statistical-analysis.md §3).

#ifndef CC_ANALYZER_ANALYSIS_PRE_ANALYSIS_MANAGER_H_
#define CC_ANALYZER_ANALYSIS_PRE_ANALYSIS_MANAGER_H_

#include <optional>
#include <string>
#include <vector>

#include "analyzer/analysis/analysis_manager.h"
#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/clangsa.h"
#include "common/skiplist_handler.h"

namespace cc::analyzer {

// The settings of the CTU collect phase (Python: ctu_data).
struct CtuData {
  std::string ctu_dir;
  std::optional<CtuMappingTool> mapping;
};

// Collects the CTU data and the statistics of the Clang SA `actions`, on
// `jobs` threads (Python: run_pre_analysis()). Actions that `skip_handlers`
// (if not null) skip are left out.
void RunPreAnalysis(const std::vector<const BuildAction*>& actions,
                    const AnalyzerConfig& clangsa_config, int jobs,
                    const common::SkipListHandlers* skip_handlers,
                    const std::optional<CtuData>& ctu_data,
                    const std::optional<StatisticsData>& statistics);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_ANALYSIS_PRE_ANALYSIS_MANAGER_H_
