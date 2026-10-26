// Port of codechecker_analyzer/analyzers/clangsa/statistics.py and of
// codechecker_statistics_collector: statistical analysis
// (statistical-analysis.md).

#ifndef CC_ANALYZER_STATS_STATISTICS_H_
#define CC_ANALYZER_STATS_STATISTICS_H_

#include <optional>
#include <string>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/analyzer.h"

namespace cc::analyzer {

// Returns the Clang command that collects statistics about `source`, or
// nullopt if Clang has no collector checkers (Python:
// build_stat_coll_cmd()).
std::optional<std::vector<std::string>> StatisticsCollectCommand(
    const BuildAction& action, const AnalyzerConfig& config,
    const std::string& source);

// Returns the Clang SA arguments that make the statistics checkers read
// the statistics in `stats_dir` (Python: checker_analyze_cfg()).
std::vector<std::string> StatisticsAnalyzeConfig(const std::string& stats_dir);

// Returns the special return value statistics file of `stats_dir`.
std::string SpecialReturnStatsFile(const std::string& stats_dir);

// Turns the collector output in `input_dir` into the statistics files of
// `output_dir` (Python: post_process_stats.process()).
void PostProcessStats(const std::string& input_dir,
                      const std::string& output_dir, int min_sample_count,
                      double relevance_threshold);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_STATS_STATISTICS_H_
