#include "client/cmd/sum.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/report_server_constants.h"
#include "api/thrift/report_server_types.h"
#include "client/client.h"
#include "client/cmd/cmd_line_client.h"
#include "util/argparse.h"
#include "util/json.h"
#include "util/print.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"

namespace cc::client::cmd {
namespace {

namespace argparse = util::argparse;

// A review status that has a column of its own, and the key of its count
// in the JSON output.
struct ReviewStatusColumn {
  ReviewStatus::type status;
  const char* json_key;
};

// The review statuses that have a column of their own, in column order.
constexpr ReviewStatusColumn kReviewStatusColumns[] = {
    {ReviewStatus::UNREVIEWED, "unreviewed"},
    {ReviewStatus::CONFIRMED, "confirmed"},
    {ReviewStatus::FALSE_POSITIVE, "false_positive"},
    {ReviewStatus::INTENTIONAL, "intentional"}};

// A count for each status of kReviewStatusColumns, in the same order.
using ReviewStatusCounts = std::array<int64_t, std::size(kReviewStatusColumns)>;

// The counts of one checker in the checker table.
struct CheckerStatistics {
  std::string checker;
  std::string severity;  // The name of the severity, for example "HIGH".
  int64_t reports = 0;   // The reports that the report filter selects.
  // The reports of each status of kReviewStatusColumns, whatever the
  // --review-status filter is.
  ReviewStatusCounts by_review_status = {};
};

// What the server returns for the tables.
struct Statistics {
  std::vector<CheckerStatistics> checkers;  // In the order of the table.
  api::OrderedMap<Severity::type, int64_t> severities;
  CheckerStatusVerificationDetails details;
  std::vector<std::vector<std::string>> labels;  // One list per detail.
};

// Returns the counts per checker of the reports of the runs `run_ids` (all
// runs if it is empty) that `filter` selects: one page of MAX_QUERY_SIZE
// checkers, ordered by name.
Result<CheckerCounts> GetCheckerCounts(ResultsClient& client,
                                       const std::vector<int64_t>& run_ids,
                                       const ReportFilter& filter) {
  // Python sends no compare data, and the server takes empty compare data
  // as none.
  CheckerCounts counts;
  const Result<void> called = client.Call(
      "getCheckerCounts", [&](codeCheckerDBAccessClient& service) {
        service.getCheckerCounts(counts, run_ids, filter, CompareData(),
                                 g_report_server_constants.MAX_QUERY_SIZE,
                                 /*offset=*/0);
      });
  if (!called.ok()) return Result<CheckerCounts>(called.error());
  return Result<CheckerCounts>(std::move(counts));
}

// Returns the counts of GetCheckerCounts() for `filter` with only the
// review status `status`, by checker name (Python: get_statistics()). Of a
// checker counted twice, which happens for two severities when uniqueing
// is off, the last count is kept, as in a Python dict.
Result<std::map<std::string, int64_t>> CountsOfReviewStatus(
    ResultsClient& client, const std::vector<int64_t>& run_ids,
    ReportFilter filter, ReviewStatus::type status) {
  using Counts = std::map<std::string, int64_t>;
  filter.__set_reviewStatus({status});
  const Result<CheckerCounts> counts =
      GetCheckerCounts(client, run_ids, filter);
  if (!counts.ok()) return Result<Counts>(counts.error());
  Counts by_name;
  for (const CheckerCount& count : counts.value()) {
    by_name[count.name] = count.count;
  }
  return Result<Counts>(std::move(by_name));
}

// Returns the rows of the checker table, highest severity first.
Result<std::vector<CheckerStatistics>> GetCheckerStatistics(
    ResultsClient& client, const std::vector<int64_t>& run_ids,
    const ReportFilter& filter) {
  using Rows = std::vector<CheckerStatistics>;
  const Result<CheckerCounts> all = GetCheckerCounts(client, run_ids, filter);
  if (!all.ok()) return Result<Rows>(all.error());
  // Python: a dict by name, so a checker counted twice keeps its first
  // place and its last count.
  api::OrderedMap<std::string, CheckerCount> by_name;
  for (const CheckerCount& count : all.value()) by_name[count.name] = count;

  std::vector<std::map<std::string, int64_t>> by_review_status;
  for (const ReviewStatusColumn& column : kReviewStatusColumns) {
    Result<std::map<std::string, int64_t>> counts =
        CountsOfReviewStatus(client, run_ids, filter, column.status);
    if (!counts.ok()) return Result<Rows>(counts.error());
    by_review_status.push_back(std::move(counts).value());
  }

  std::vector<CheckerCount> sorted;
  for (const auto& [name, count] : by_name) sorted.push_back(count);
  // Stable, so checkers of the same severity stay in name order.
  std::ranges::stable_sort(sorted, std::ranges::greater(),
                           &CheckerCount::severity);
  Rows rows;
  for (const CheckerCount& count : sorted) {
    CheckerStatistics row{
        .checker = count.name,
        .severity = EnumName(_Severity_VALUES_TO_NAMES, count.severity),
        .reports = count.count};
    for (size_t i = 0; i < by_review_status.size(); ++i) {
      const auto found = by_review_status[i].find(count.name);
      if (found != by_review_status[i].end()) {
        row.by_review_status[i] = found->second;
      }
    }
    rows.push_back(std::move(row));
  }
  return Result<Rows>(std::move(rows));
}

// Makes the API calls of the tables, in the order of the reference. The
// checker details and labels are fetched for the JSON output too, which
// does not show them.
Result<Statistics> GetStatistics(ResultsClient& client,
                                 const std::vector<int64_t>& run_ids,
                                 const ReportFilter& filter) {
  Statistics statistics;
  Result<std::vector<CheckerStatistics>> checkers =
      GetCheckerStatistics(client, run_ids, filter);
  if (!checkers.ok()) return Result<Statistics>(checkers.error());
  statistics.checkers = std::move(checkers).value();
  Result<void> called = client.Call(
      "getSeverityCounts", [&](codeCheckerDBAccessClient& service) {
        service.getSeverityCounts(statistics.severities, run_ids, filter,
                                  CompareData());
      });
  if (!called.ok()) return Result<Statistics>(called.error());
  called = client.Call(
      "getCheckerStatusVerificationDetails",
      [&](codeCheckerDBAccessClient& service) {
        service.getCheckerStatusVerificationDetails(statistics.details,
                                                    run_ids, filter);
      });
  if (!called.ok()) return Result<Statistics>(called.error());
  std::vector<Checker> checker_ids;
  for (const auto& [id, detail] : statistics.details) {
    Checker checker;
    checker.__set_analyzerName(detail.analyzerName);
    checker.__set_checkerId(detail.checkerName);
    checker_ids.push_back(std::move(checker));
  }
  called = client.Call(
      "getCheckerLabels", [&](codeCheckerDBAccessClient& service) {
        service.getCheckerLabels(statistics.labels, checker_ids);
      });
  if (!called.ok()) return Result<Statistics>(called.error());
  return Result<Statistics>(std::move(statistics));
}

// Returns what follows `prefix` in `label`, which starts with it, up to
// the next `prefix` (Python: label.split(prefix)[1]).
std::string_view LabelValue(std::string_view label, std::string_view prefix) {
  const std::string_view rest = label.substr(prefix.size());
  return rest.substr(0, rest.find(prefix));
}

// Returns the guidelines of a checker with the `labels` ("key:value") as
// the checker list shows them: "<guideline>: <rule>, <rule>" for each
// "guideline:<guideline>" label, the rules being the values of the
// "<guideline>:<rule>" labels, joined with ";  " (Python:
// formatted_guidelines()).
std::string GuidelinesText(const std::vector<std::string>& labels) {
  constexpr std::string_view kGuidelinePrefix = "guideline:";
  std::vector<std::string> guidelines;
  for (const std::string& label : labels) {
    // Python looks for labels that start with "guideline"; any other such
    // label would make it fail.
    if (!label.starts_with(kGuidelinePrefix)) continue;
    const std::string_view guideline = LabelValue(label, kGuidelinePrefix);
    const std::string rule_prefix = std::string(guideline) + ":";
    std::vector<std::string> rules;
    for (const std::string& rule_label : labels) {
      if (rule_label.starts_with(rule_prefix)) {
        rules.emplace_back(LabelValue(rule_label, rule_prefix));
      }
    }
    guidelines.push_back(
        std::format("{}: {}", guideline, util::Join(rules, ", ")));
  }
  return util::Join(guidelines, ";  ");
}

// Prints the checker table as a JSON list of objects on one line.
void PrintCheckersJson(const std::vector<CheckerStatistics>& checkers) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* list = yyjson_mut_arr(doc);
  for (const CheckerStatistics& checker : checkers) {
    yyjson_mut_val* object = yyjson_mut_arr_add_obj(doc, list);
    yyjson_mut_obj_add_val(doc, object, "checker",
                           util::MakeJsonString(doc, checker.checker));
    yyjson_mut_obj_add_val(doc, object, "severity",
                           util::MakeJsonString(doc, checker.severity));
    yyjson_mut_obj_add_sint(doc, object, "reports", checker.reports);
    for (size_t i = 0; i < checker.by_review_status.size(); ++i) {
      yyjson_mut_obj_add_sint(doc, object, kReviewStatusColumns[i].json_key,
                              checker.by_review_status[i]);
    }
  }
  util::Print(util::DumpJson(list));
}

// Prints the checker table in `format`, with a "Total" row at the end.
void PrintCheckersTable(std::string_view format,
                        const std::vector<CheckerStatistics>& checkers) {
  const std::vector<std::string> header = {
      "Checker", "Severity", "Unreviewed", "Confirmed", "False positive",
      "Intentional", "All reports"};
  std::vector<std::vector<std::string>> rows;
  int64_t total_reports = 0;
  ReviewStatusCounts totals = {};
  for (const CheckerStatistics& checker : checkers) {
    std::vector<std::string> row = {checker.checker, checker.severity};
    for (size_t i = 0; i < totals.size(); ++i) {
      row.push_back(std::to_string(checker.by_review_status[i]));
      totals[i] += checker.by_review_status[i];
    }
    row.push_back(std::to_string(checker.reports));
    total_reports += checker.reports;
    rows.push_back(std::move(row));
  }
  std::vector<std::string> footer = {"Total", "-"};
  for (const int64_t total : totals) footer.push_back(std::to_string(total));
  footer.push_back(std::to_string(total_reports));
  rows.push_back(std::move(footer));
  util::Print(util::twodim::ToString(format, header, rows,
                                     util::twodim::Footer::kSeparate));
}

// Prints the severity table in `format`, highest severity first, with a
// "Total" row at the end.
void PrintSeveritiesTable(
    std::string_view format,
    const api::OrderedMap<Severity::type, int64_t>& severities) {
  std::vector<std::pair<Severity::type, int64_t>> sorted(severities.begin(),
                                                         severities.end());
  std::ranges::stable_sort(sorted, std::ranges::greater(),
                           &std::pair<Severity::type, int64_t>::first);
  std::vector<std::vector<std::string>> rows;
  int64_t total = 0;
  for (const auto& [severity, count] : sorted) {
    rows.push_back({EnumName(_Severity_VALUES_TO_NAMES, severity),
                    std::to_string(count)});
    total += count;
  }
  rows.push_back({"Total", std::to_string(total)});
  util::Print(util::twodim::ToString(format, {"Severity", "All reports"},
                                     rows, util::twodim::Footer::kSeparate));
}

// Prints the list of every checker of `details`, with the guidelines of
// its `labels` (one list per detail, in the same order), in `format`.
void PrintCheckerListTable(
    std::string_view format, const CheckerStatusVerificationDetails& details,
    const std::vector<std::vector<std::string>>& labels) {
  const bool any_disabled =
      std::ranges::any_of(details, [](const auto& entry) {
        return !entry.second.disabled.empty();
      });
  std::vector<std::vector<std::string>> rows;
  size_t index = 0;
  for (const auto& [id, detail] : details) {
    const bool enabled_in_all = detail.disabled.empty();
    // Python formats the bool with the width of its column in the "rows"
    // format, which prints it as a number aligned to the right.
    const std::string enabled =
        format == "rows"
            ? std::format("{:>{}}", enabled_in_all ? 1 : 0,
                          any_disabled ? 5 : 4)
            : (enabled_in_all ? "True" : "False");
    // The server returns one label list for each checker.
    const std::string guidelines =
        index < labels.size() ? GuidelinesText(labels[index]) : "";
    rows.push_back({detail.checkerName, guidelines,
                    EnumName(_Severity_VALUES_TO_NAMES, detail.severity),
                    enabled, std::to_string(detail.closed),
                    std::to_string(detail.outstanding)});
    ++index;
  }
  util::Print(util::twodim::ToString(
      format,
      {"Checker Name", "guideline", "Severity",
       "Enabled in all selected runs", "Closed Reports",
       "Outstanding Reports"},
      rows));
}

}  // namespace

Result<void> ListResultTypes(const argparse::Namespace& args) {
  Result<ResultsClient> client = SetupReportClient(args);
  if (!client.ok()) return Result<void>(client.error());
  // -a: no run ids, which the server takes as every run.
  std::vector<int64_t> run_ids;
  if (!args.Has("all_results")) {
    const Result<std::vector<RunData>> runs =
        FindRuns(client.value(), args.GetList("names"));
    if (!runs.ok()) return Result<void>(runs.error());
    for (const RunData& run : runs.value()) run_ids.push_back(run.runId);
  }

  const Result<ReportFilter> filter = ParseReportFilter(client.value(), args);
  if (!filter.ok()) return Result<void>(filter.error());
  const Result<Statistics> statistics =
      GetStatistics(client.value(), run_ids, filter.value());
  if (!statistics.ok()) return Result<void>(statistics.error());
  const std::string& format = args.Get("output_format");
  if (format == "json") {
    PrintCheckersJson(statistics.value().checkers);
    return Result<void>();
  }
  PrintCheckersTable(format, statistics.value().checkers);
  PrintSeveritiesTable(format, statistics.value().severities);
  PrintCheckerListTable(format, statistics.value().details,
                        statistics.value().labels);
  return Result<void>();
}

}  // namespace cc::client::cmd
