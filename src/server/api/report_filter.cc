#include "server/api/report_filter.h"

#include <stdint.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/report_server_types.h"
#include "api/thrift/shared_types.h"
#include "server/api/request_failed.h"
#include "server/api/thrift_enum_helper.h"
#include "server/database/connection.h"
#include "server/database/sql.h"
#include "util/datetime.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::server::api {
namespace {

// Returns the values of `severities`.
std::vector<int64_t> SeverityValues(
    const std::vector<Severity::type>& severities) {
  std::vector<int64_t> values;
  for (const Severity::type severity : severities) {
    values.push_back(static_cast<int64_t>(severity));
  }
  return values;
}

// Returns the names of `statuses`.
ApiResult<std::vector<std::string>> ReviewStatusNames(
    const std::vector<ReviewStatus::type>& statuses) {
  using Names = ApiResult<std::vector<std::string>>;
  std::vector<std::string> names;
  for (const ReviewStatus::type status : statuses) {
    ApiResult<std::string> name = ReviewStatusName(status);
    if (!name.ok()) return Names(name.error());
    names.push_back(std::move(name).value());
  }
  return Names(std::move(names));
}

// Returns "<column> <op> ?" with `value` bound.
db::Sql Compare(std::string_view column, std::string_view op,
                db::Param value) {
  db::Sql sql(std::format("{} {} ", column, op));
  return sql.Bind(std::move(value));
}

// Returns the query of the reports whose bug path goes through a file that
// `file_condition` selects, or that have a bug note or other event there
// (Python: get_reports_by_bugpath_filter()).
db::Sql ReportsByBugPath(const db::Sql& file_condition) {
  db::Sql query(
      "SELECT reports.id FROM reports JOIN files "
      "ON files.id = reports.file_id WHERE ");
  query.Add(file_condition)
      .Add(" UNION SELECT report_path_data.report_id FROM report_path_data "
           "JOIN report_path_data_files ON report_path_data.report_id = "
           "report_path_data_files.report_path_data_id JOIN files "
           "ON files.id = report_path_data_files.file_id WHERE ")
      .Add(file_condition);
  return query;
}

// Returns the query of the reports that are entirely in the files that
// `file_condition` selects (Python:
// get_reports_by_bugpath_filter_for_single_origin()). As in the reference,
// the excluded reports are joined with every other file, so a filter that
// selects two files excludes every report (known-issues #9).
db::Sql ReportsBySingleOrigin(const db::Sql& file_condition) {
  db::Sql query = ReportsByBugPath(file_condition);
  query
      .Add(" EXCEPT SELECT reports.id FROM reports JOIN files "
           "ON files.id != reports.file_id WHERE ")
      .Add(file_condition)
      .Add(" EXCEPT SELECT report_path_data.report_id FROM report_path_data "
           "JOIN report_path_data_files ON report_path_data.report_id = "
           "report_path_data_files.report_path_data_id JOIN files "
           "ON files.id != report_path_data_files.file_id WHERE ")
      .Add(file_condition);
  return query;
}

// Returns the condition on "files" of the source components `names`
// (Python: process_source_component_filter()). The auto-generated "Other"
// component is the files of no component, if there are components at all.
ApiResult<db::Sql> SourceComponentCondition(
    db::Connection& db, const std::vector<std::string>& names) {
  using Condition = ApiResult<db::Sql>;
  std::vector<db::Sql> any;
  for (const std::string& name : names) {
    if (name == kOtherComponentName) {
      const Result<std::optional<std::string>> count =
          db::QueryValue(db, "SELECT count(*) FROM source_components", {});
      if (!count.ok()) return Condition(DatabaseError(count.error()));
      if (count.value().value_or("0") == "0") continue;
      any.emplace_back(
          "files.id NOT IN (SELECT source_component_files.file_id "
          "FROM source_component_files)");
    } else {
      db::Sql in_component(
          "files.id IN (SELECT source_component_files.file_id "
          "FROM source_component_files "
          "WHERE source_component_files.source_component_name = ");
      any.push_back(in_component.Bind(name).Add(")"));
    }
  }
  return Condition(any.empty() ? db::Sql("true") : db::Or(any));
}

// Returns the query of the report hashes of the runs `run_ids` and the run
// history tags `tag_ids` that were open at `open_reports_date` (0: now)
// (Python: get_diff_bug_id_query()).
db::Sql DiffBugIdQuery(const std::vector<int64_t>& run_ids,
                       const std::vector<int64_t>& tag_ids,
                       int64_t open_reports_date) {
  db::Select select = {.columns = db::Sql("reports.bug_id"),
                       .distinct = true,
                       .from = db::Sql("reports")};
  if (!run_ids.empty()) {
    select.where.push_back(db::In("reports.run_id", run_ids));
    if (tag_ids.empty() && open_reports_date == 0) {
      select.where.emplace_back("reports.fixed_at IS NULL");
    }
  }
  if (!tag_ids.empty()) {
    select.from.Add(
        " LEFT OUTER JOIN run_histories "
        "ON run_histories.run_id = reports.run_id");
    select.where.push_back(db::In("run_histories.id", tag_ids));
    select.where.push_back(
        OpenReportsAt("reports", db::Sql("run_histories.time")));
  }
  if (open_reports_date != 0) {
    db::Sql date;
    date.Bind(TimeParam(open_reports_date));
    select.where.push_back(OpenReportsAt("reports", date));
  }
  return db::Build(select);
}

// Returns the query of the ids of the runs `run_ids` that have the run
// history tags `tag_ids` (Python: get_diff_run_id_query()).
db::Sql DiffRunIdQuery(const std::vector<int64_t>& run_ids,
                       const std::vector<int64_t>& tag_ids) {
  db::Select select = {.columns = db::Sql("runs.id"),
                       .distinct = true,
                       .from = db::Sql("runs")};
  if (!run_ids.empty()) select.where.push_back(db::In("runs.id", run_ids));
  if (!tag_ids.empty()) {
    select.from.Add(
        " LEFT OUTER JOIN run_histories ON run_histories.run_id = runs.id");
    select.where.push_back(db::In("run_histories.id", tag_ids));
  }
  return db::Build(select);
}

// The condition of a compare filter and the tables it needs.
struct CompareSql {
  std::optional<db::Sql> condition = {};  // nullopt: no condition.
  JoinedTables joins = {};
};

// Returns the condition of comparing the runs `run_ids` (with the baseline
// filters of `filter`) with `cmp_data`, which may be null (Python:
// process_cmp_data_filter()).
ApiResult<CompareSql> ProcessCompareData(const std::vector<int64_t>& run_ids,
                                         const ReportFilter& filter,
                                         const CompareData* cmp_data) {
  using Compared = ApiResult<CompareSql>;
  const std::vector<int64_t>& base_tags = filter.runTag;
  const int64_t base_date = filter.openReportsDate;
  const bool cmp_empty = cmp_data == nullptr ||
                         (cmp_data->runIds.empty() &&
                          cmp_data->runTag.empty() &&
                          cmp_data->openReportsDate == 0);
  if (cmp_empty) {
    if (run_ids.empty() && base_tags.empty() && base_date == 0) {
      return Compared(CompareSql());
    }
    std::vector<db::Sql> all;
    if (!run_ids.empty()) all.push_back(db::In("reports.run_id", run_ids));
    if (!base_tags.empty()) {
      all.push_back(db::In("run_histories.id", base_tags));
      all.push_back(OpenReportsAt("reports", db::Sql("run_histories.time")));
    }
    if (base_date != 0) {
      db::Sql date;
      date.Bind(TimeParam(base_date));
      all.push_back(OpenReportsAt("reports", date));
    }
    return Compared(CompareSql{.condition = db::And(all),
                               .joins = {.runs = !run_ids.empty(),
                                         .run_histories = !base_tags.empty()}});
  }
  const db::Sql base = DiffBugIdQuery(run_ids, base_tags, base_date);
  const db::Sql base_runs = DiffRunIdQuery(run_ids, base_tags);
  const db::Sql compared = DiffBugIdQuery(
      cmp_data->runIds, cmp_data->runTag, cmp_data->openReportsDate);
  const db::Sql compared_runs =
      DiffRunIdQuery(cmp_data->runIds, cmp_data->runTag);
  // Returns "<left> <op> <right>" as the hashes, and the runs `runs`.
  auto diff = [](const db::Sql& left, std::string_view op,
                 const db::Sql& right, const db::Sql& runs) {
    db::Sql hashes = left;
    hashes.Add(std::format(" {} ", op)).Add(right);
    return Compared(
        CompareSql{.condition = db::And({db::In("reports.bug_id", hashes),
                                         db::In("reports.run_id", runs)}),
                   .joins = {.runs = true}});
  };
  switch (cmp_data->diffType) {
    case DiffType::NEW:
      return diff(compared, "EXCEPT", base, compared_runs);
    case DiffType::RESOLVED:
      return diff(base, "EXCEPT", compared, base_runs);
    case DiffType::UNRESOLVED:
      return diff(base, "INTERSECT", compared, compared_runs);
  }
  return Compared(ApiError{
      .code = ErrorCode::DATABASE,
      .message = std::format("Unsupported diff type: {}",
                             static_cast<int>(cmp_data->diffType))});
}

// Returns the annotation filter of `filter`: the values of each key, the
// keys in the order they first occur (Python: the defaultdict of
// annotations).
cc::api::OrderedMap<std::string, std::vector<std::string>> GroupAnnotations(
    const std::vector<Pair>& annotations) {
  cc::api::OrderedMap<std::string, std::vector<std::string>> groups;
  for (const Pair& annotation : annotations) {
    groups[annotation.first].push_back(annotation.second);
  }
  return groups;
}

// Returns the conditions of the date filters of `filter` (Python: the date
// part of process_report_filter()).
ApiResult<std::vector<db::Sql>> DateConditions(const ReportFilter& filter) {
  using Conditions = ApiResult<std::vector<db::Sql>>;
  std::vector<db::Sql> all;
  if (filter.__isset.firstDetectionDate) {
    all.push_back(Compare("reports.detected_at", ">=",
                          TimeParam(filter.firstDetectionDate)));
  }
  if (filter.__isset.fixDate) {
    // As in the reference, the fix date filters the detection date.
    all.push_back(
        Compare("reports.detected_at", "<", TimeParam(filter.fixDate)));
  }
  if (filter.__isset.date) {
    if (filter.date.__isset.detected) {
      const DateInterval& detected = filter.date.detected;
      if (detected.before != 0) {
        all.push_back(Compare("reports.detected_at", "<=",
                              TimeParam(detected.before)));
      }
      if (detected.after != 0) {
        all.push_back(Compare("reports.detected_at", ">=",
                              TimeParam(detected.after)));
      }
    }
    if (filter.date.__isset.fixed) {
      const DateInterval& fixed = filter.date.fixed;
      if (fixed.before != 0) {
        all.push_back(
            Compare("reports.fixed_at", "<=", TimeParam(fixed.before)));
      }
      if (fixed.after != 0) {
        all.push_back(
            Compare("reports.fixed_at", ">=", TimeParam(fixed.after)));
      }
    }
  }
  if (!filter.runHistoryTag.empty()) {
    std::vector<db::Sql> any;
    for (const std::string& tag_date : filter.runHistoryTag) {
      const ApiResult<int64_t> time = ParseTime(tag_date);
      if (!time.ok()) return Conditions(time.error());
      const std::string date = util::FormatDatabaseTime(time.value());
      db::Sql open = Compare("reports.detected_at", "<=", date);
      open.Add(" AND (reports.fixed_at IS NULL OR reports.fixed_at >= ")
          .Bind(date)
          .Add(")");
      any.push_back(db::And({open}));
    }
    all.push_back(db::Or(any));
  }
  return Conditions(std::move(all));
}

// Returns the condition of the annotation filter of `filter`, nullopt if it
// has none (Python: the annotation part of process_report_filter()).
std::optional<db::Sql> AnnotationCondition(const ReportFilter& filter,
                                           bool keep_all_annotations) {
  if (!filter.__isset.annotations) return std::nullopt;
  std::vector<db::Sql> any;
  for (const auto& [key, values] : GroupAnnotations(filter.annotations)) {
    std::vector<db::Sql> matches;
    for (const std::string& value : values) {
      matches.push_back(db::ILike(db::Sql("report_annotations.value"),
                                  Conv(value)));
    }
    if (keep_all_annotations) {
      matches.insert(matches.begin(),
                     Compare("report_annotations.\"key\"", "!=", key));
      any.push_back(db::Or(matches));
    } else {
      any.push_back(db::And({Compare("report_annotations.\"key\"", "=", key),
                             db::Or(matches)}));
    }
  }
  if (any.empty()) return std::nullopt;
  return db::Or(any);
}

}  // namespace

ComponentValues ParseComponentValues(std::string_view value) {
  ComponentValues values;
  for (const std::string& line : util::Split(value, "\n")) {
    const std::string_view path = util::Strip(line);
    if (path.empty()) continue;
    if (path.front() == '+') values.include.emplace_back(path.substr(1));
    if (path.front() == '-') values.skip.emplace_back(path.substr(1));
  }
  return values;
}

std::string Conv(std::string_view filter) {
  std::string converted(filter);
  std::ranges::replace(converted, '*', '%');
  return converted;
}

std::string EscapeLike(std::string_view text) {
  std::string escaped;
  for (const char c : text) {
    if (c == '\\' || c == '%' || c == '_') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

std::string TimeParam(int64_t timestamp) {
  return util::FormatDatabaseTime(util::FromTimestamp(timestamp));
}

ApiResult<int64_t> ParseTime(const std::string& text) {
  // strptime() needs every part, the fraction too.
  const std::optional<int64_t> time = util::ParseDatabaseTime(text);
  if (!time || text.size() < 21 || text[10] != ' ' || text[19] != '.') {
    return ApiResult<int64_t>(ApiError{
        .code = ErrorCode::GENERAL,
        .message = std::format("time data {} does not match format "
                               "'%Y-%m-%d %H:%M:%S.%f'",
                               util::Repr(text))});
  }
  return ApiResult<int64_t>(*time);
}

db::Sql OpenReportsAt(std::string_view table, const db::Sql& date) {
  db::Sql open(std::format("({}.detected_at <= ", table));
  open.Add(date).Add(std::format(" AND ({0}.fixed_at IS NULL OR "
                                 "{0}.fixed_at > ",
                                 table));
  return open.Add(date).Add("))");
}

db::Sql OutstandingCondition() {
  return db::And({db::In("reports.review_status",
                         std::vector<std::string>{"unreviewed", "confirmed"}),
                  db::In("reports.detection_status",
                         std::vector<std::string>{"new", "unresolved",
                                                  "reopened"})});
}

ApiResult<ReportFilterSql> ProcessReportFilter(
    db::Connection& db, const std::vector<int64_t>& run_ids,
    const ReportFilter& filter, const CompareData* cmp_data,
    bool keep_all_annotations) {
  using FilterSql = ApiResult<ReportFilterSql>;
  ApiResult<CompareSql> compare_result =
      ProcessCompareData(run_ids, filter, cmp_data);
  if (!compare_result.ok()) return FilterSql(compare_result.error());
  CompareSql compared = std::move(compare_result).value();
  ReportFilterSql result = {.joins = compared.joins};
  if (filter.__isset.reportHash && filter.reportHash.empty()) {
    return FilterSql(ReportFilterSql{.condition = db::Sql("false")});
  }
  std::vector<db::Sql> all;
  if (compared.condition) all.push_back(*std::move(compared.condition));

  // Returns the OR of the ILIKE of `column` and each of `patterns`.
  auto any_like = [](std::string_view column,
                     const std::vector<std::string>& patterns) {
    std::vector<db::Sql> any;
    for (const std::string& pattern : patterns) {
      any.push_back(db::ILike(db::Sql(column), Conv(pattern)));
    }
    return db::Or(any);
  };
  if (!filter.filepath.empty()) {
    const db::Sql files = any_like("files.filepath", filter.filepath);
    if (filter.fileMatchesAnyPoint) {
      all.push_back(db::In("reports.id", ReportsByBugPath(files)));
    } else {
      all.push_back(files);
      result.joins.files = true;
    }
  }
  if (!filter.checkerMsg.empty()) {
    all.push_back(any_like("reports.checker_message", filter.checkerMsg));
  }
  if (!filter.analyzerNames.empty() || !filter.checkerName.empty() ||
      !filter.severity.empty()) {
    if (!filter.analyzerNames.empty()) {
      all.push_back(any_like("checkers.analyzer_name", filter.analyzerNames));
    }
    if (!filter.checkerName.empty()) {
      all.push_back(any_like("checkers.checker_name", filter.checkerName));
    }
    if (!filter.severity.empty()) {
      all.push_back(
          db::In("checkers.severity", SeverityValues(filter.severity)));
    }
    result.joins.checkers = true;
  }
  if (!filter.runName.empty()) {
    all.push_back(any_like("runs.name", filter.runName));
    result.joins.runs = true;
  }
  if (!filter.reportHash.empty()) {
    std::vector<db::Sql> any;
    std::vector<std::string> exact;
    for (const std::string& hash : filter.reportHash) {
      if (hash.find('*') != std::string::npos) {
        any.push_back(db::ILike(db::Sql("reports.bug_id"), Conv(hash)));
      } else {
        exact.push_back(hash);
      }
    }
    if (!exact.empty()) any.push_back(db::In("reports.bug_id", exact));
    all.push_back(db::Or(any));
  }
  if (!filter.cleanupPlanNames.empty()) {
    std::vector<db::Sql> any;
    for (const std::string& name : filter.cleanupPlanNames) {
      db::Sql hashes(
          "SELECT DISTINCT cleanup_plan_report_hashes.bug_hash "
          "FROM cleanup_plan_report_hashes "
          "WHERE cleanup_plan_report_hashes.cleanup_plan_id IN "
          "(SELECT DISTINCT cleanup_plans.id FROM cleanup_plans "
          "WHERE cleanup_plans.name = ");
      any.push_back(db::In("reports.bug_id", hashes.Bind(name).Add(")")));
    }
    all.push_back(db::Or(any));
  }
  if (!filter.reportStatus.empty()) {
    std::vector<db::Sql> any;
    const auto has = [&filter](ReportStatus::type status) {
      return std::ranges::find(filter.reportStatus, status) !=
             filter.reportStatus.end();
    };
    if (has(ReportStatus::OUTSTANDING)) any.push_back(OutstandingCondition());
    if (has(ReportStatus::CLOSED)) {
      db::Sql closed("NOT ");
      any.push_back(closed.Add(OutstandingCondition()));
    }
    if (!any.empty()) all.push_back(db::Or(any));
  }
  if (!filter.detectionStatus.empty()) {
    ApiResult<std::vector<std::string>> names =
        DetectionStatusNames(filter.detectionStatus);
    if (!names.ok()) return FilterSql(names.error());
    all.push_back(db::In("reports.detection_status", names.value()));
  }
  if (!filter.reviewStatus.empty()) {
    ApiResult<std::vector<std::string>> names =
        ReviewStatusNames(filter.reviewStatus);
    if (!names.ok()) return FilterSql(names.error());
    all.push_back(db::In("reports.review_status", names.value()));
  }
  ApiResult<std::vector<db::Sql>> dates = DateConditions(filter);
  if (!dates.ok()) return FilterSql(dates.error());
  for (db::Sql& condition : dates.value()) all.push_back(std::move(condition));
  if (!filter.componentNames.empty()) {
    const ApiResult<db::Sql> component_files =
        SourceComponentCondition(db, filter.componentNames);
    if (!component_files.ok()) return FilterSql(component_files.error());
    const db::Sql& files = component_files.value();
    if (filter.componentMatchesAnyPoint) {
      all.push_back(db::In("reports.id", ReportsByBugPath(files)));
    } else if (filter.fullReportPathInComponent) {
      all.push_back(db::In("reports.id", ReportsBySingleOrigin(files)));
    } else {
      all.push_back(files);
      result.joins.files = true;
    }
  }
  if (filter.__isset.bugPathLength) {
    if (filter.bugPathLength.__isset.min) {
      all.push_back(
          Compare("reports.path_length", ">=", filter.bugPathLength.min));
    }
    if (filter.bugPathLength.__isset.max) {
      all.push_back(
          Compare("reports.path_length", "<=", filter.bugPathLength.max));
    }
  }
  if (std::optional<db::Sql> annotations =
          AnnotationCondition(filter, keep_all_annotations)) {
    all.push_back(*std::move(annotations));
    result.uses_annotations = true;
  }
  result.condition = all.empty() ? db::Sql("true") : db::And(all);
  return FilterSql(std::move(result));
}

void ApplyReportFilter(const ReportFilterSql& filter,
                       const JoinedTables& joined, db::Select* select) {
  if (filter.joins.checkers && !joined.checkers) {
    select->from.Add(" JOIN checkers ON reports.checker_id = checkers.id");
  }
  if (filter.joins.files && !joined.files) {
    select->from.Add(" LEFT OUTER JOIN files ON reports.file_id = files.id");
  }
  if (filter.joins.runs && !joined.runs) {
    select->from.Add(" LEFT OUTER JOIN runs ON runs.id = reports.run_id");
  }
  if (filter.joins.run_histories && !joined.run_histories) {
    select->from.Add(
        " LEFT OUTER JOIN run_histories "
        "ON run_histories.run_id = reports.run_id");
  }
  select->where.push_back(filter.condition);
}

ApiResult<void> ProcessRunFilter(db::Connection& db, const RunFilter& filter,
                                 db::Select* select) {
  if (!filter.ids.empty()) {
    select->where.push_back(db::In("runs.id", filter.ids));
  }
  if (!filter.names.empty()) {
    if (filter.exactMatch) {
      select->where.push_back(db::In("runs.name", filter.names));
    } else {
      std::vector<db::Sql> any;
      for (const std::string& name : filter.names) {
        any.push_back(
            db::ILikeEscaped(db::Sql("runs.name"), Conv(EscapeLike(name))));
      }
      select->where.push_back(db::Or(any));
    }
  }
  if (filter.beforeTime != 0) {
    select->where.push_back(
        Compare("runs.date", "<", TimeParam(filter.beforeTime)));
  }
  if (filter.afterTime != 0) {
    select->where.push_back(
        Compare("runs.date", ">", TimeParam(filter.afterTime)));
  }
  // Adds "runs.date <op> <date of the run `name`>", if there is such a run.
  auto compare_run_date = [&db, select](const std::string& name,
                                        std::string_view op) {
    const Result<db::QueryResult> rows =
        db.Query("SELECT runs.date FROM runs WHERE runs.name = ?", {name});
    if (!rows.ok()) return ApiResult<void>(DatabaseError(rows.error()));
    if (rows.value().rows.empty()) return ApiResult<void>();
    const std::optional<std::string> date =
        rows.value().rows.front().OptionalText(0);
    select->where.push_back(
        Compare("runs.date", op, date ? db::Param(*date) : db::Param(nullptr)));
    return ApiResult<void>();
  };
  if (!filter.beforeRun.empty()) {
    ApiResult<void> added = compare_run_date(filter.beforeRun, "<");
    if (!added.ok()) return added;
  }
  if (!filter.afterRun.empty()) return compare_run_date(filter.afterRun, ">");
  return ApiResult<void>();
}

bool IsUnset(const RunFilter& filter) {
  const auto& set = filter.__isset;
  return !set.ids && !set.names && !set.exactMatch && !set.beforeTime &&
         !set.afterTime && !set.beforeRun && !set.afterRun;
}

void ProcessRunHistoryFilter(const std::vector<int64_t>& run_ids,
                             const RunHistoryFilter& filter,
                             db::Select* select) {
  if (!run_ids.empty()) {
    select->where.push_back(db::In("run_histories.run_id", run_ids));
  }
  if (!filter.tagNames.empty()) {
    std::vector<db::Sql> any;
    for (const std::string& name : filter.tagNames) {
      any.push_back(db::ILikeEscaped(db::Sql("run_histories.version_tag"),
                                     Conv(EscapeLike(name))));
    }
    select->where.push_back(db::Or(any));
  }
  if (!filter.tagIds.empty()) {
    select->where.push_back(db::In("run_histories.id", filter.tagIds));
  }
  if (filter.__isset.stored) {
    if (filter.stored.before != 0) {
      select->where.push_back(Compare("run_histories.time", "<=",
                                      TimeParam(filter.stored.before)));
    }
    if (filter.stored.after != 0) {
      select->where.push_back(Compare("run_histories.time", ">=",
                                      TimeParam(filter.stored.after)));
    }
  }
}

ApiResult<std::vector<std::string>> DetectionStatusNames(
    const std::vector<DetectionStatus::type>& statuses) {
  using Names = ApiResult<std::vector<std::string>>;
  std::vector<std::string> names;
  for (const DetectionStatus::type status : statuses) {
    ApiResult<std::string> name = DetectionStatusName(status);
    if (!name.ok()) return Names(name.error());
    names.push_back(std::move(name).value());
  }
  return Names(std::move(names));
}

}  // namespace cc::server::api
