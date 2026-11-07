#include "client/cmd/cmd_line_client.h"

#include <stdint.h>

#include <algorithm>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/report_server_constants.h"
#include "api/thrift/report_server_types.h"
#include "client/client.h"
#include "common/cli/verbose.h"
#include "util/argparse.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::client::cmd {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");

// The options that parse_report_filter() counts as filter options, in its
// order, by destination.
constexpr std::string_view kFilterOptions[] = {
    "severity", "detection_status", "review_status", "checker_name",
    "file_path", "bug_path_length", "checker_msg", "analyzer_name",
    "component", "report_hash", "open_reports_date", "detected_at",
    "fixed_at", "detected_before", "detected_after", "fixed_before",
    "fixed_after", "tag", "report_status"};

// Returns all items that `fetch_page` gives, one page of MAX_QUERY_SIZE
// items at a time, until a page is shorter (the loop of Python's
// get_run_data() and get_run_results()). `fetch_page(limit, offset, page)`
// makes one API call, which fills `page`.
template <typename T>
Result<std::vector<T>> FetchAllPages(
    const std::function<Result<void>(int64_t limit, int64_t offset,
                                     std::vector<T>& page)>& fetch_page) {
  const int64_t limit = g_report_server_constants.MAX_QUERY_SIZE;
  std::vector<T> items;
  for (int64_t offset = 0;; offset += limit) {
    std::vector<T> page;
    const Result<void> fetched = fetch_page(limit, offset, page);
    if (!fetched.ok()) return Result<std::vector<T>>(fetched.error());
    const bool last = static_cast<int64_t>(page.size()) < limit;
    items.insert(items.end(), std::make_move_iterator(page.begin()),
                 std::make_move_iterator(page.end()));
    if (last) return Result<std::vector<T>>(std::move(items));
  }
}

// Returns the values of the API enum `Enum`, whose generated name table is
// `names`, that `values` name in any case (Python:
// [<Enum>._NAMES_TO_VALUES[x.upper()] for x in values]). An unknown name
// fails like Python's KeyError, which ends the command with a traceback on
// stdout (known-issues #22).
template <typename Enum>
Result<std::vector<Enum>> EnumValues(const std::map<int, const char*>& names,
                                     const std::vector<std::string>& values) {
  std::vector<Enum> result;
  for (const std::string& value : values) {
    const std::string upper = util::AsciiUpper(value);
    const auto found =
        std::ranges::find_if(names, [&upper](const auto& entry) {
          return entry.second == upper;
        });
    if (found == names.end()) {
      util::Print("KeyError: " + util::Repr(upper));
      return Result<std::vector<Enum>>(Error{});
    }
    result.push_back(static_cast<Enum>(found->first));
  }
  return Result<std::vector<Enum>>(std::move(result));
}

// Returns the Unix time of the TIMESTAMP option `dest` of `args` (see
// TimestampOf()); nullopt if it is not given.
Result<std::optional<int64_t>> TimestampOption(const argparse::Namespace& args,
                                               std::string_view dest) {
  using Time = std::optional<int64_t>;
  if (!args.Has(dest)) return Result<Time>(Time());
  const Result<int64_t> time = TimestampOf(args.Get(dest));
  if (!time.ok()) return Result<Time>(time.error());
  return Result<Time>(Time(time.value()));
}

// Returns the interval of the TIMESTAMP options `before` and `after` of
// `args`; nullopt if neither is given.
Result<std::optional<DateInterval>> DateIntervalOption(
    const argparse::Namespace& args, std::string_view before,
    std::string_view after) {
  using Interval = std::optional<DateInterval>;
  const Result<std::optional<int64_t>> before_time =
      TimestampOption(args, before);
  if (!before_time.ok()) return Result<Interval>(before_time.error());
  const Result<std::optional<int64_t>> after_time =
      TimestampOption(args, after);
  if (!after_time.ok()) return Result<Interval>(after_time.error());
  if (!before_time.value() && !after_time.value()) {
    return Result<Interval>(Interval());
  }
  DateInterval interval;
  if (before_time.value()) interval.__set_before(*before_time.value());
  if (after_time.value()) interval.__set_after(*after_time.value());
  return Result<Interval>(Interval(std::move(interval)));
}

// Returns the bug path length range of a --bug-path-length value,
// "min:max", where a part is used only if it is a number (Python: the
// bug_path_length part of parse_report_filter_offline()).
BugPathLengthRange BugPathLengthOf(std::string_view value) {
  const auto number = [](std::string_view text) -> std::optional<int64_t> {
    const bool digits = !text.empty() && std::ranges::all_of(text, [](char c) {
      return c >= '0' && c <= '9';
    });
    return digits ? util::ParseInt(text) : std::nullopt;
  };
  const std::vector<std::string> lengths = util::Split(value, ":");
  BugPathLengthRange range;
  if (const std::optional<int64_t> min = number(lengths[0])) {
    range.__set_min(*min);
  }
  if (lengths.size() > 1) {
    if (const std::optional<int64_t> max = number(lengths[1])) {
      range.__set_max(*max);
    }
  }
  return range;
}

// Sets the date conditions of the filter options in `args` in `filter`.
Result<void> SetDateFilters(const argparse::Namespace& args,
                            ReportFilter& filter) {
  const Result<std::optional<int64_t>> open_reports =
      TimestampOption(args, "open_reports_date");
  if (!open_reports.ok()) return Result<void>(open_reports.error());
  if (open_reports.value()) {
    filter.__set_openReportsDate(*open_reports.value());
  }
  const Result<std::optional<int64_t>> detected_at =
      TimestampOption(args, "detected_at");
  if (!detected_at.ok()) return Result<void>(detected_at.error());
  if (detected_at.value()) {
    filter.__set_firstDetectionDate(*detected_at.value());
  }
  const Result<std::optional<int64_t>> fixed_at =
      TimestampOption(args, "fixed_at");
  if (!fixed_at.ok()) return Result<void>(fixed_at.error());
  if (fixed_at.value()) filter.__set_fixDate(*fixed_at.value());
  const Result<std::optional<DateInterval>> detected =
      DateIntervalOption(args, "detected_before", "detected_after");
  if (!detected.ok()) return Result<void>(detected.error());
  const Result<std::optional<DateInterval>> fixed =
      DateIntervalOption(args, "fixed_before", "fixed_after");
  if (!fixed.ok()) return Result<void>(fixed.error());
  if (detected.value() || fixed.value()) {
    ReportDate date;
    if (detected.value()) date.__set_detected(*detected.value());
    if (fixed.value()) date.__set_fixed(*fixed.value());
    filter.__set_date(std::move(date));
  }
  return Result<void>();
}

// Returns the report filter of the filter options in `args`, without the
// version tags, which need the server (Python:
// parse_report_filter_offline()).
Result<ReportFilter> ParseReportFilterOffline(const argparse::Namespace& args) {
  ReportFilter filter;
  if (args.Has("severity")) {
    Result<std::vector<Severity::type>> values = EnumValues<Severity::type>(
        _Severity_VALUES_TO_NAMES, args.GetList("severity"));
    if (!values.ok()) return Result<ReportFilter>(values.error());
    filter.__set_severity(std::move(values).value());
  }
  if (args.Has("detection_status")) {
    Result<std::vector<DetectionStatus::type>> values =
        EnumValues<DetectionStatus::type>(_DetectionStatus_VALUES_TO_NAMES,
                                          args.GetList("detection_status"));
    if (!values.ok()) return Result<ReportFilter>(values.error());
    filter.__set_detectionStatus(std::move(values).value());
  }
  if (args.Has("review_status")) {
    Result<std::vector<ReviewStatus::type>> values =
        EnumValues<ReviewStatus::type>(_ReviewStatus_VALUES_TO_NAMES,
                                       args.GetList("review_status"));
    if (!values.ok()) return Result<ReportFilter>(values.error());
    filter.__set_reviewStatus(std::move(values).value());
  }
  if (args.Has("checker_name")) {
    filter.__set_checkerName(args.GetList("checker_name"));
  }
  if (args.Has("file_path")) filter.__set_filepath(args.GetList("file_path"));
  if (args.Has("bug_path_length")) {
    filter.__set_bugPathLength(BugPathLengthOf(args.Get("bug_path_length")));
  }
  filter.__set_isUnique(args.Has("uniqueing") && args.Get("uniqueing") == "on");
  if (args.Has("checker_msg")) {
    filter.__set_checkerMsg(args.GetList("checker_msg"));
  }
  if (args.Has("analyzer_name")) {
    filter.__set_analyzerNames(args.GetList("analyzer_name"));
  }
  if (args.Has("component")) {
    filter.__set_componentNames(args.GetList("component"));
  }
  if (args.Has("report_hash")) {
    filter.__set_reportHash(args.GetList("report_hash"));
  }
  if (const Result<void> dates = SetDateFilters(args, filter); !dates.ok()) {
    return Result<ReportFilter>(dates.error());
  }
  const bool anywhere = args.GetBool("anywhere_on_report_path");
  const bool single_origin = args.GetBool("single_origin_report");
  filter.__set_fileMatchesAnyPoint(anywhere);
  filter.__set_componentMatchesAnyPoint(anywhere);
  filter.__set_fullReportPathInComponent(single_origin);
  if (args.Has("report_status")) {
    Result<std::vector<ReportStatus::type>> values =
        EnumValues<ReportStatus::type>(_ReportStatus_VALUES_TO_NAMES,
                                       args.GetList("report_status"));
    if (!values.ok()) return Result<ReportFilter>(values.error());
    filter.__set_reportStatus(std::move(values).value());
  }
  if (anywhere && !args.Has("file_path") && !args.Has("component")) {
    kLog.Warning("The flag --anywhere-on-report-path is meaningful only if "
                 "--file or --component is used.");
  }
  if (single_origin && !args.Has("component")) {
    kLog.Warning("The flag --single-origin-report is meaningful only if "
                 "--component is used.");
  }
  return Result<ReportFilter>(std::move(filter));
}

// Returns the report filter of the preset that --filter-preset names, after
// checking that no other filter option differs from its default (the
// preset part of Python's parse_report_filter()).
Result<ReportFilter> PresetReportFilter(ResultsClient& client,
                                        const argparse::Namespace& args) {
  std::vector<std::string> conflicting;
  for (const std::string_view dest : kFilterOptions) {
    if (!args.Has(dest)) continue;
    const std::optional<std::vector<std::string>> default_value =
        DefaultFilterValue(dest);
    if (default_value && args.GetList(dest) == *default_value) continue;
    // Python names the options after their destinations, so --file is
    // "--file-path".
    conflicting.push_back("--" + util::Replace(dest, "_", "-"));
  }
  if (!conflicting.empty()) {
    kLog.Error(std::format(
        "Cannot combine --filter-preset with other filter arguments ({}). "
        "Either use a preset or specify filters on the command line, not "
        "both.",
        util::Join(conflicting, ", ")));
    return Result<ReportFilter>(Error{});
  }
  const std::string& name = args.Get("filter_preset_name");
  kLog.Info(std::format("Loading filter preset '{}'...", name));
  std::vector<FilterPreset> presets;
  const Result<void> called = client.Call(
      "listFilterPreset", [&presets](codeCheckerDBAccessClient& service) {
        service.listFilterPreset(presets);
      });
  if (!called.ok()) return Result<ReportFilter>(called.error());
  const auto preset = std::ranges::find(presets, name, &FilterPreset::name);
  if (preset == presets.end()) {
    kLog.Error(std::format("Filter preset '{}' not found!", name));
    kLog.Info("Use 'CodeChecker cmd filter-preset list' to see available "
              "presets.");
    return Result<ReportFilter>(Error{});
  }
  return Result<ReportFilter>(preset->reportFilter);
}

// Returns the ids of the run history entries whose version tag matches one
// of `tags` (the tag part of Python's parse_report_filter()).
Result<std::vector<int64_t>> RunTagIds(ResultsClient& client,
                                       const std::vector<std::string>& tags) {
  RunHistoryFilter history_filter;
  history_filter.__set_tagNames(tags);
  std::vector<RunHistoryData> histories;
  // Python sends no run ids and no offset, which the server takes as all
  // runs and offset 0.
  const Result<void> called = client.Call(
      "getRunHistory", [&](codeCheckerDBAccessClient& service) {
        service.getRunHistory(histories, std::vector<int64_t>(),
                              g_report_server_constants.MAX_QUERY_SIZE,
                              /*offset=*/0, history_filter);
      });
  if (!called.ok()) return Result<std::vector<int64_t>>(called.error());
  std::vector<int64_t> ids;
  for (const RunHistoryData& history : histories) ids.push_back(history.id);
  return Result<std::vector<int64_t>>(std::move(ids));
}

}  // namespace

std::optional<std::vector<std::string>> DefaultFilterValue(
    std::string_view dest) {
  using Values = std::vector<std::string>;
  if (dest == "review_status") return Values{"unreviewed", "confirmed"};
  if (dest == "detection_status") {
    return Values{"new", "reopened", "unresolved"};
  }
  if (dest == "uniqueing") return Values{"off"};
  if (dest == "anywhere_on_report_path" || dest == "single_origin_report") {
    return Values{"False"};
  }
  return std::nullopt;
}

void SetupCmdLogger(const argparse::Namespace& args) {
  const bool machine_readable =
      args.Has("output_format") && args.Get("output_format") != "table";
  util::SetupLogger(
      common::cli::Verbose(args),
      machine_readable ? util::Stream::kStderr : util::Stream::kStdout);
}

Result<std::string> ValidTime(const std::string& value) {
  std::vector<int64_t> parts;
  for (const std::string& part : util::Split(value, ":")) {
    const std::optional<int64_t> number = util::ParseInt(part);
    if (!number) return Result<std::string>(util::IntValueError(part));
    parts.push_back(*number);
  }
  // Python unpacks the parts into year, month, day, hour, minute, second.
  if (parts.size() > 6) {
    return Result<std::string>(
        Error{.message = "too many values to unpack (expected 6)"});
  }
  parts.resize(6, 0);
  const Result<int64_t> time =
      util::MakeTime(parts[0], parts[1], parts[2], parts[3], parts[4],
                     parts[5]);
  if (!time.ok()) return Result<std::string>(time.error());
  return Result<std::string>(std::to_string(time.value()));
}

Result<int64_t> TimestampOf(const std::string& value) {
  const std::optional<int64_t> seconds =
      util::ToTimestamp(util::ParseInt(value).value_or(0));
  if (!seconds) {
    // Python's time.mktime() raises an exception that nothing catches.
    util::Print("OverflowError: mktime argument out of range");
    return Result<int64_t>(Error{});
  }
  return Result<int64_t>(*seconds);
}

std::string AnalyzerStatisticsText(const AnalyzerStatisticsData& statistics) {
  std::vector<std::string> parts;
  for (const auto& [analyzer, stats] : statistics) {
    parts.push_back(std::format("{} ({}/{})", analyzer,
                                stats.successful + stats.failed,
                                stats.successful));
  }
  return util::Join(parts, ", ");
}

std::string EnumName(const std::map<int, const char*>& names, int value) {
  const auto found = names.find(value);
  return found != names.end() ? std::string(found->second)
                              : std::to_string(value);
}

Result<std::vector<RunData>> GetRunData(
    ResultsClient& client, const RunFilter& filter,
    const std::optional<RunSortMode>& sort_mode) {
  // Python sends no sort mode for None, but the generated client always
  // sends one, so the order the server uses for None is sent instead.
  RunSortMode mode;
  if (sort_mode) {
    mode = *sort_mode;
  } else {
    mode.__set_type(RunSortType::DATE);
    mode.__set_ord(Order::DESC);
  }
  return FetchAllPages<RunData>(
      [&](int64_t limit, int64_t offset, std::vector<RunData>& page) {
        return client.Call(
            "getRunData", [&](codeCheckerDBAccessClient& service) {
              service.getRunData(page, filter, limit, offset, mode);
            });
      });
}

Result<std::vector<RunData>> GetRunsByName(
    ResultsClient& client, const std::vector<std::string>& names) {
  RunFilter filter;
  filter.__set_names(names);
  return GetRunData(client, filter, /*sort_mode=*/std::nullopt);
}

Result<std::vector<RunData>> FindRuns(ResultsClient& client,
                                      const std::vector<std::string>& names) {
  Result<std::vector<RunData>> runs = GetRunsByName(client, names);
  if (runs.ok() && runs.value().empty()) {
    kLog.Warning("No runs were found!");
    return Result<std::vector<RunData>>(Error{});
  }
  return runs;
}

Result<std::vector<ReportData>> GetRunResults(
    ResultsClient& client, const std::vector<int64_t>& run_ids,
    const ReportFilter& filter, bool details) {
  // Python sends no sort modes and no compare data. The generated client
  // always sends them, so it sends the sort order the server uses without
  // sort modes, and empty compare data, which the server takes as none.
  SortMode by_severity;
  by_severity.__set_type(SortType::SEVERITY);
  by_severity.__set_ord(Order::DESC);
  const std::vector<SortMode> sort_modes = {by_severity};
  const CompareData no_compare_data;
  return FetchAllPages<ReportData>(
      [&](int64_t limit, int64_t offset, std::vector<ReportData>& page) {
        return client.Call(
            "getRunResults", [&](codeCheckerDBAccessClient& service) {
              service.getRunResults(page, run_ids, limit, offset, sort_modes,
                                    filter, no_compare_data, details);
            });
      });
}

Result<void> CheckExistingSourceComponents(
    ResultsClient& client, const std::vector<std::string>& names) {
  std::vector<std::string> missing;
  for (const std::string& name : names) {
    std::vector<SourceComponentData> components;
    const Result<void> called = client.Call(
        "getSourceComponents", [&](codeCheckerDBAccessClient& service) {
          service.getSourceComponents(components, {name});
        });
    if (!called.ok()) return called;
    if (components.empty()) missing.push_back(name);
  }
  if (!missing.empty()) {
    kLog.Error("The following source components don't exist: " +
               util::Join(missing, ", "));
    return Result<void>(Error{});
  }
  return Result<void>();
}

void CheckDeprecatedArgUsage(const argparse::Namespace& args) {
  if (args.Has("detected_at")) {
    kLog.Warning(
        "\"--detected-at\" option has been deprecated. Use "
        "--detected-before/--detected-after options to filter the results by "
        "detection date. For more information see the help.");
  }
  if (args.Has("fixed_at")) {
    kLog.Warning(
        "\"--fixed-at\" option has been deprecated. Use "
        "--fixed-before/--fixed-after options to filter the results by fix "
        "date. For more information see the help.");
  }
}

Result<ResultsClient> SetupReportClient(const argparse::Namespace& args) {
  SetupCmdLogger(args);
  CheckDeprecatedArgUsage(args);
  Result<ResultsClient> client = SetupClient(args.Get("product_url"));
  if (!client.ok() || !args.Has("component")) return client;
  const Result<void> checked = CheckExistingSourceComponents(
      client.value(), args.GetList("component"));
  if (!checked.ok()) return Result<ResultsClient>(checked.error());
  return client;
}

Result<ReportFilter> ParseReportFilter(ResultsClient& client,
                                       const argparse::Namespace& args) {
  if (args.Has("filter_preset_name")) return PresetReportFilter(client, args);
  Result<ReportFilter> filter = ParseReportFilterOffline(args);
  if (!filter.ok() || !args.Has("tag")) return filter;
  const Result<std::vector<int64_t>> tag_ids =
      RunTagIds(client, args.GetList("tag"));
  if (!tag_ids.ok()) return Result<ReportFilter>(tag_ids.error());
  if (!tag_ids.value().empty()) filter.value().__set_runTag(tag_ids.value());
  return filter;
}

}  // namespace cc::client::cmd
