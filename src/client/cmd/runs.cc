#include "client/cmd/runs.h"

#include <stdint.h>
#include <yyjson.h>

#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/report_server_constants.h"
#include "api/thrift/report_server_types.h"
#include "client/client.h"
#include "client/cmd/cmd_line.h"
#include "client/cmd/cmd_line_client.h"
#include "util/argparse.h"
#include "util/datetime.h"
#include "util/json.h"
#include "util/print.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"

namespace cc::client::cmd {
namespace {

namespace argparse = util::argparse;

// The names of the enabled checkers of a run, by analyzer.
using EnabledCheckers = std::map<std::string, std::set<std::string>>;

// Returns the run filter of the run selection options in `args`, which
// exclude each other (Python: process_run_filter_conditions()).
Result<RunFilter> RunFilterOf(const argparse::Namespace& args) {
  RunFilter filter;
  if (args.Has("names")) {
    filter.__set_names(args.GetList("names"));
    filter.__set_exactMatch(false);
  } else if (args.Has("all_after_run")) {
    filter.__set_afterRun(args.Get("all_after_run"));
  } else if (args.Has("all_before_run")) {
    filter.__set_beforeRun(args.Get("all_before_run"));
  } else if (args.Has("all_after_time") || args.Has("all_before_time")) {
    const bool after = args.Has("all_after_time");
    const Result<int64_t> time =
        TimestampOf(args.Get(after ? "all_after_time" : "all_before_time"));
    if (!time.ok()) return Result<RunFilter>(time.error());
    if (after) {
      filter.__set_afterTime(time.value());
    } else {
      filter.__set_beforeTime(time.value());
    }
  }
  return Result<RunFilter>(std::move(filter));
}

// Returns the sort mode of --sort and --order in `args`.
RunSortMode RunSortModeOf(const argparse::Namespace& args) {
  RunSortMode mode;
  // --sort only takes the names of kRunSortTypes.
  for (const auto& [type, name] : kRunSortTypes) {
    if (name == args.Get("sort_type")) mode.__set_type(type);
  }
  mode.__set_ord(args.Get("sort_order") == "asc" ? Order::ASC : Order::DESC);
  return mode;
}

// Returns the checkers that were enabled in the analyses of the run
// `run_id`, by analyzer.
Result<EnabledCheckers> EnabledCheckersOf(ResultsClient& client,
                                          int64_t run_id) {
  AnalysisInfoFilter filter;
  filter.__set_runId(run_id);
  std::vector<AnalysisInfo> infos;
  const Result<void> called = client.Call(
      "getAnalysisInfo", [&](codeCheckerDBAccessClient& service) {
        service.getAnalysisInfo(infos, filter,
                                g_report_server_constants.MAX_QUERY_SIZE,
                                /*offset=*/0);
      });
  if (!called.ok()) return Result<EnabledCheckers>(called.error());
  EnabledCheckers enabled;
  for (const AnalysisInfo& info : infos) {
    for (const auto& [analyzer, checkers] : info.checkers) {
      std::set<std::string>& names = enabled[analyzer];
      for (const auto& [name, checker] : checkers) {
        if (checker.__isset.enabled && checker.enabled) names.insert(name);
      }
    }
  }
  return Result<EnabledCheckers>(std::move(enabled));
}

// Returns the JSON object of `run` for --details: with the statistics of
// its latest analysis, each with the sorted list of the checkers its
// analyzer had enabled (the --details part of Python's handle_list_runs()).
Result<yyjson_mut_val*> DetailedRunJson(yyjson_mut_doc* doc,
                                        ResultsClient& client, RunData run) {
  const Result<EnabledCheckers> enabled = EnabledCheckersOf(client, run.runId);
  if (!enabled.ok()) return Result<yyjson_mut_val*>(enabled.error());
  AnalyzerStatisticsData statistics;
  // Python asks without a run history id, which the server ignores when a
  // run id is given.
  const Result<void> called = client.Call(
      "getAnalysisStatistics", [&](codeCheckerDBAccessClient& service) {
        service.getAnalysisStatistics(statistics, run.runId,
                                      /*runHistoryId=*/0);
      });
  if (!called.ok()) return Result<yyjson_mut_val*>(called.error());
  run.__set_analyzerStatistics(std::move(statistics));
  yyjson_mut_val* object = ToJson(doc, run);
  // Python adds enabledCheckers to each statistics object, after its
  // fields.
  yyjson_mut_val* all_statistics =
      yyjson_mut_obj_get(object, "analyzerStatistics");
  yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(all_statistics);
  while (yyjson_mut_val* analyzer = yyjson_mut_obj_iter_next(&iter)) {
    yyjson_mut_val* checkers = yyjson_mut_arr(doc);
    const auto found = enabled.value().find(util::JsonString(analyzer));
    if (found != enabled.value().end()) {
      for (const std::string& name : found->second) {
        yyjson_mut_arr_append(checkers, util::MakeJsonString(doc, name));
      }
    }
    yyjson_mut_obj_add_val(doc, yyjson_mut_obj_iter_get_val(analyzer),
                           "enabledCheckers", checkers);
  }
  return Result<yyjson_mut_val*>(object);
}

// Prints `runs` as JSON: a list of objects, each with the run's name as its
// only key and the run as its value. With `details`, the statistics have
// more fields (see DetailedRunJson()).
Result<void> PrintRunsJson(ResultsClient& client,
                           const std::vector<RunData>& runs, bool details) {
  const util::MutableJsonDocument doc = util::NewJsonDocument();
  yyjson_mut_val* list = yyjson_mut_arr(doc.get());
  for (const RunData& run : runs) {
    yyjson_mut_val* object = nullptr;
    if (details) {
      const Result<yyjson_mut_val*> detailed =
          DetailedRunJson(doc.get(), client, run);
      if (!detailed.ok()) return Result<void>(detailed.error());
      object = detailed.value();
    } else {
      object = ToJson(doc.get(), run);
    }
    yyjson_mut_val* item = yyjson_mut_obj(doc.get());
    yyjson_mut_obj_add(item, util::MakeJsonString(doc.get(), run.name),
                       object);
    yyjson_mut_arr_append(list, item);
  }
  // Python's json.dump() writes no newline at the end.
  util::Write(util::Stream::kStdout, util::DumpJsonIndent(list, 4));
  return Result<void>();
}

// Returns the analyzer statistics of `run` as the tables show them:
// "<analyzer> (<all files>/<successful files>)", joined with ", ".
std::string AnalyzerStatisticsText(const RunData& run) {
  std::vector<std::string> parts;
  for (const auto& [analyzer, stats] : run.analyzerStatistics) {
    parts.push_back(std::format("{} ({}/{})", analyzer,
                                stats.successful + stats.failed,
                                stats.successful));
  }
  return util::Join(parts, ", ");
}

// Prints `runs` as a table in `format`: "plaintext", "rows", "table" or
// "csv".
void PrintRunsTable(std::string_view format, const std::vector<RunData>& runs) {
  const std::vector<std::string> header = {
      "Name", "Number of unresolved reports", "Analyzer statistics",
      "Storage date", "Version tag", "Duration", "Description",
      "CodeChecker version"};
  std::vector<std::vector<std::string>> rows;
  for (const RunData& run : runs) {
    // -1 while a store into the run is in progress.
    const std::string duration =
        run.duration > -1
            ? util::FormatTimedelta(run.duration * util::kMicrosecondsPerSecond)
            : "Not finished";
    rows.push_back({run.name, std::to_string(run.resultCount),
                    AnalyzerStatisticsText(run), run.runDate, run.versionTag,
                    duration, run.description, run.codeCheckerVersion});
  }
  util::Print(util::twodim::ToString(format, header, rows));
}

}  // namespace

Result<void> ListRuns(const argparse::Namespace& args) {
  SetupCmdLogger(args);
  Result<ResultsClient> client = SetupClient(args.Get("product_url"));
  if (!client.ok()) return Result<void>(client.error());
  const Result<RunFilter> filter = RunFilterOf(args);
  if (!filter.ok()) return Result<void>(filter.error());
  const Result<std::vector<RunData>> runs =
      GetRunData(client.value(), filter.value(), RunSortModeOf(args));
  if (!runs.ok()) return Result<void>(runs.error());
  const std::string& format = args.Get("output_format");
  if (format == "json") {
    return PrintRunsJson(client.value(), runs.value(),
                         args.GetBool("details"));
  }
  PrintRunsTable(format, runs.value());
  return Result<void>();
}

}  // namespace cc::client::cmd
