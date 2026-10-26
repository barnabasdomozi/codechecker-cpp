#include "analyzer/analysis/pre_analysis_manager.h"

#include <stddef.h>

#include <atomic>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "analyzer/context/run_command.h"
#include "analyzer/ctu/ctu_manager.h"
#include "analyzer/ctu/ctu_triple_arch.h"
#include "analyzer/stats/statistics.h"
#include "platform/filesystem.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/parallel.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/uuid.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

// Python: collect_statistics(): runs the collector checkers on `action` and
// saves their output.
void CollectStatistics(const BuildAction& action, const AnalyzerConfig& config,
                       const StatisticsData& statistics) {
  const std::optional<std::vector<std::string>> command =
      StatisticsCollectCommand(action, config, action.source);
  if (!command) {
    kLog.Debug("Can not collect statistical data.");
    return;
  }
  kLog.DebugAnalyzer(util::ShlexJoin(*command));
  const Result<CommandOutput> output = RunProc(*command, {});
  if (!output.ok()) {
    kLog.Error("Failed to collect statistics for " + action.source);
    return;
  }
  kLog.DebugAnalyzer(output.value().out);
  kLog.DebugAnalyzer(output.value().err);
  if (output.value().return_code != 0) {
    kLog.Error("Failed to collect statistics for " + action.source);
    return;
  }
  kLog.Debug("Running statistics collectors for " + action.source +
             " was sucesssful.");
  const std::string file = util::pypath::Join(
      {statistics.stat_tmp_dir,
       util::pypath::Basename(action.source) + util::Uuid4() + ".stat"});
  (void)util::WriteFile(file, output.value().out + output.value().err);
}

}  // namespace

void RunPreAnalysis(const std::vector<const BuildAction*>& actions,
                    const AnalyzerConfig& clangsa_config, int jobs,
                    const common::SkipListHandlers* skip_handlers,
                    const std::optional<CtuData>& ctu_data,
                    const std::optional<StatisticsData>& statistics) {
  kLog.Info("Pre-analysis started.");
  if (ctu_data) kLog.Info("Collecting data for ctu analysis.");
  if (statistics) {
    kLog.Info("Collecting data for statistical analysis.");
    platform::RemoveTree(statistics->stat_tmp_dir);
    (void)platform::MakeDirs(statistics->stat_tmp_dir);
  }
  std::atomic<int> processed = 0;
  const int total = static_cast<int>(actions.size());
  util::ParallelFor(jobs, actions.size(), [&](size_t i) {
    const BuildAction& action = *actions[i];
    const int number = processed.fetch_add(1) + 1;
    if (skip_handlers != nullptr &&
        common::ShouldSkip(*skip_handlers, action.source)) {
      return;
    }
    if (action.analyzer_type != "clangsa") return;
    kLog.Info(std::format("[{}/{}] {}", number, total,
                          util::pypath::Basename(action.source)));
    if (ctu_data) {
      kLog.Debug("running CTU pre analysis");
      const std::string triple_arch =
          TripleArch(action, action.source, clangsa_config);
      if (clangsa_config.ctu_on_demand) {
        GenerateInvocationList(triple_arch, action, action.source,
                               clangsa_config);
      } else {
        GenerateAst(triple_arch, action, action.source, clangsa_config);
      }
      if (ctu_data->mapping) {
        MapFunctions(triple_arch, action, action.source, clangsa_config,
                     ctu_data->mapping->tool);
      } else {
        kLog.Error(std::format("Pre-analysis failed for {}: expected str, "
                               "bytes or os.PathLike object, not NoneType",
                               action.source));
      }
    }
    if (statistics) {
      kLog.Debug("running statistics pre analysis");
      CollectStatistics(action, clangsa_config, *statistics);
    }
  });
  if (ctu_data) {
    MergeExtdefMappings(ctu_data->ctu_dir,
                        ctu_data->mapping ? ctu_data->mapping->map_file : "");
  }
  if (statistics) {
    PostProcessStats(statistics->stat_tmp_dir, statistics->stats_out_dir,
                     statistics->min_sample_count.value_or(0),
                     statistics->relevance_threshold.value_or(0));
    if (util::pypath::Exists(statistics->stat_tmp_dir)) {
      kLog.Debug("Cleaning up temporary statistics directory");
      platform::RemoveTree(statistics->stat_tmp_dir);
    }
  }
  kLog.Info("Pre-analysis finished.");
}

}  // namespace cc::analyzer
