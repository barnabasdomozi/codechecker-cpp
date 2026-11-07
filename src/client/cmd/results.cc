#include "client/cmd/results.h"

#include <stdint.h>
#include <yyjson.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "api/thrift/report_server_types.h"
#include "client/client.h"
#include "client/cmd/cmd_line.h"
#include "client/cmd/cmd_line_client.h"
#include "util/argparse.h"
#include "util/json.h"
#include "util/print.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/twodim.h"
#include "util/unicode.h"

namespace cc::client::cmd {
namespace {

namespace argparse = util::argparse;

// The longest message the tables show; longer ones are cut and end with
// "...".
constexpr int kMaxMessageLength = 50;

// Prints `reports` as a JSON list on one line, each report with the name of
// its run added as "runName". `run_names` maps run ids to names.
void PrintReportsJson(const std::vector<ReportData>& reports,
                      const std::map<int64_t, std::string>& run_names) {
  const util::MutableJsonDocument doc = util::NewJsonDocument();
  yyjson_mut_val* list = yyjson_mut_arr(doc.get());
  for (const ReportData& report : reports) {
    yyjson_mut_val* object = ToJson(doc.get(), report);
    const auto run_name = run_names.find(report.runId);
    yyjson_mut_obj_add_val(
        doc.get(), object, "runName",
        util::MakeJsonString(doc.get(), run_name != run_names.end()
                                            ? run_name->second
                                            : std::string()));
    yyjson_mut_arr_append(list, object);
  }
  util::Print(util::DumpJson(list));
}

// Returns the message of `report` as the tables show it: whitespace runs
// become one space, and a long message is cut.
std::string TableMessage(const ReportData& report) {
  static const util::Regex& kWhitespace = *new util::Regex(R"(\s+)");
  std::string message = kWhitespace.ReplaceAll(report.checkerMsg, " ");
  if (util::CodePointCount(message) > kMaxMessageLength) {
    message =
        std::string(util::CodePointSlicePrefix(message, kMaxMessageLength)) +
        "...";
  }
  return message;
}

// Prints `reports` as a table in `format`: "plaintext", "rows", "table" or
// "csv". Without reports, there is no header either, and only an empty
// line is printed.
void PrintReportsTable(std::string_view format,
                       const std::vector<ReportData>& reports) {
  std::vector<std::vector<std::string>> rows;
  for (const ReportData& report : reports) {
    std::string file = report.checkedFile;
    if (report.__isset.line) file += " @ " + std::to_string(report.line);
    const std::string detection_status =
        report.__isset.detectionStatus
            ? EnumName(_DetectionStatus_VALUES_TO_NAMES,
                       report.detectionStatus)
            : "N/A";
    rows.push_back(
        {file, report.checkerId,
         EnumName(_Severity_VALUES_TO_NAMES, report.severity),
         TableMessage(report),
         report.__isset.bugPathLength ? std::to_string(report.bugPathLength)
                                      : "",
         report.analyzerName,
         EnumName(_ReviewStatus_VALUES_TO_NAMES, report.reviewData.status),
         detection_status});
  }
  // Python also has a variant of the table with blame information, but it
  // is never chosen: details are only fetched for JSON.
  std::vector<std::string> header;
  if (!rows.empty()) {
    header = {"File", "Checker", "Severity", "Message", "Bug path length",
              "Analyzer name", "Review status", "Detection status"};
  }
  util::Print(util::twodim::ToString(format, header, rows));
}

}  // namespace

Result<void> ListResults(const argparse::Namespace& args) {
  Result<ResultsClient> client = SetupReportClient(args);
  if (!client.ok()) return Result<void>(client.error());
  const Result<std::vector<RunData>> runs =
      FindRuns(client.value(), args.GetList("names"));
  if (!runs.ok()) return Result<void>(runs.error());
  std::vector<int64_t> run_ids;
  std::map<int64_t, std::string> run_names;
  for (const RunData& run : runs.value()) {
    run_ids.push_back(run.runId);
    run_names[run.runId] = run.name;
  }

  const Result<ReportFilter> filter = ParseReportFilter(client.value(), args);
  if (!filter.ok()) return Result<void>(filter.error());
  const std::string& format = args.Get("output_format");
  const bool details = args.GetBool("details") && format == "json";
  const Result<std::vector<ReportData>> reports =
      GetRunResults(client.value(), run_ids, filter.value(), details);
  if (!reports.ok()) return Result<void>(reports.error());
  if (format == "json") {
    PrintReportsJson(reports.value(), run_names);
  } else {
    PrintReportsTable(format, reports.value());
  }
  return Result<void>();
}

}  // namespace cc::client::cmd
