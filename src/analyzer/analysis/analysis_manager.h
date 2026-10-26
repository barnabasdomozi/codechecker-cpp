// Port of codechecker_analyzer/analysis_manager.py: the analysis jobs, run
// in a pool of threads (analyze.md §4.1, analysis-debugging.md).

#ifndef CC_ANALYZER_ANALYSIS_ANALYSIS_MANAGER_H_
#define CC_ANALYZER_ANALYSIS_ANALYSIS_MANAGER_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/metadata/metadata.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"

namespace cc::analyzer {

// The settings of statistical analysis (Python: statistics_data).
struct StatisticsData {
  std::string stats_out_dir;
  std::string stat_tmp_dir = {};
  std::optional<int> min_sample_count = {};
  std::optional<double> relevance_threshold = {};
};

// The numbers of compile commands of the analysis (Python:
// CompileCmdParseCount).
struct CompileCommandCount {
  int total = 0;
  int analyze = 0;
  int skipped = 0;
  int removed_by_uniqueing = 0;
};

// The settings of the analysis jobs (Python: the parameters of
// start_workers()).
struct WorkerOptions {
  int jobs = 1;
  std::string output_path;
  const common::SkipListHandlers* skip_handlers = nullptr;
  // The handlers that drop reports; null if reports are not dropped.
  const common::SkipListHandlers* filter_handlers = nullptr;
  const common::ReviewStatusHandler* review_status = nullptr;
  bool quiet = false;
  bool capture_analysis_output = false;
  bool generate_reproducer = false;
  std::optional<int> timeout;
  bool ctu_reanalyze_on_failure = false;
  std::optional<StatisticsData> statistics;
};

// Returns a run of `action` with its analyzer, ready for its command
// (Python: prepare_check()): the statistics configuration is added for
// Clang SA.
Result<AnalyzerRun> PrepareRun(const BuildAction& action,
                               const AnalyzerConfig& config,
                               const std::string& output_path,
                               const std::optional<StatisticsData>& statistics,
                               bool disable_ctu);

// Runs the analysis jobs `actions` (Python: start_workers()) with the
// configurations of their analyzers, and records the results in
// `metadata`.
void StartWorkers(const std::vector<BuildAction>& actions,
                  const std::map<std::string, AnalyzerConfig>& configs,
                  const WorkerOptions& options,
                  const CompileCommandCount& count, MetadataTool* metadata);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_ANALYSIS_ANALYSIS_MANAGER_H_
