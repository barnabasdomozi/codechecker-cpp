#include "client/cmd/history.h"

#include <stdint.h>
#include <yyjson.h>

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
#include "util/json.h"
#include "util/print.h"
#include "util/result.h"
#include "util/twodim.h"

namespace cc::client::cmd {
namespace {

namespace argparse = util::argparse;

// Returns the ids of the runs whose names match the -n patterns in `args`;
// no ids without -n.
Result<std::vector<int64_t>> RunIdsOf(ResultsClient& client,
                                      const argparse::Namespace& args) {
  std::vector<int64_t> run_ids;
  if (!args.Has("names")) return Result<std::vector<int64_t>>(run_ids);
  const Result<std::vector<RunData>> runs =
      GetRunsByName(client, args.GetList("names"));
  if (!runs.ok()) return Result<std::vector<int64_t>>(runs.error());
  for (const RunData& run : runs.value()) run_ids.push_back(run.runId);
  return Result<std::vector<int64_t>>(std::move(run_ids));
}

// Prints `histories` as a JSON list on one line.
void PrintHistoriesJson(const RunHistoryDataList& histories) {
  const util::MutableJsonDocument doc = util::NewJsonDocument();
  yyjson_mut_val* list = yyjson_mut_arr(doc.get());
  for (const RunHistoryData& history : histories) {
    yyjson_mut_arr_append(list, ToJson(doc.get(), history));
  }
  util::Print(util::DumpJson(list));
}

// Prints `histories` as a table in `format`: "plaintext", "rows", "table"
// or "csv".
void PrintHistoriesTable(std::string_view format,
                         const RunHistoryDataList& histories) {
  const std::vector<std::string> header = {
      "Date", "Run name", "Version tag", "User", "CodeChecker version",
      "Analyzer statistics", "Description"};
  std::vector<std::vector<std::string>> rows;
  for (const RunHistoryData& history : histories) {
    rows.push_back({history.time, history.runName, history.versionTag,
                    history.user, history.codeCheckerVersion,
                    AnalyzerStatisticsText(history.analyzerStatistics),
                    history.description});
  }
  util::Print(util::twodim::ToString(format, header, rows));
}

}  // namespace

Result<void> ListRunHistories(const argparse::Namespace& args) {
  SetupCmdLogger(args);
  Result<ResultsClient> client = SetupClient(args.Get("product_url"));
  if (!client.ok()) return Result<void>(client.error());
  // A -n pattern that matches no run gives no run ids either, which the
  // server takes as every run (known-issues #40).
  const Result<std::vector<int64_t>> run_ids = RunIdsOf(client.value(), args);
  if (!run_ids.ok()) return Result<void>(run_ids.error());

  // One page only, so at most the MAX_QUERY_SIZE newest entries. Python
  // sends no offset and no filter, which the server takes as offset 0 and
  // no filter.
  RunHistoryDataList histories;
  const Result<void> called = client.value().Call(
      "getRunHistory", [&](codeCheckerDBAccessClient& service) {
        service.getRunHistory(histories, run_ids.value(),
                              g_report_server_constants.MAX_QUERY_SIZE,
                              /*offset=*/0, RunHistoryFilter());
      });
  if (!called.ok()) return called;
  const std::string& format = args.Get("output_format");
  if (format == "json") {
    PrintHistoriesJson(histories);
  } else {
    PrintHistoriesTable(format, histories);
  }
  return Result<void>();
}

}  // namespace cc::client::cmd
