#include "server/api/report_handler.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/report_server_constants.h"
#include "api/thrift/report_server_types.h"
#include "api/thrift/shared_types.h"
#include "common/checker_labels.h"
#include "common/guidelines.h"
#include "platform/filesystem.h"
#include "server/api/mass_store_run.h"
#include "server/api/report_filter.h"
#include "server/api/request_failed.h"
#include "server/api/thrift_enum_helper.h"
#include "server/auth/permissions.h"
#include "server/database/connection.h"
#include "server/database/db_cleanup.h"
#include "server/database/sql.h"
#include "server/package_data.h"
#include "server/product.h"
#include "server/server_config.h"
#include "server/server_context.h"
#include "server/tasks/task_record.h"
#include "util/base64.h"
#include "util/compression.h"
#include "util/datetime.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::server::api {
namespace {

constexpr util::Logger kLog("server");

// The kinds of comments (Python: CommentKindValue).
constexpr int64_t kUserComment = 0;
constexpr int64_t kSystemComment = 1;

// The columns of a report and its checker that ReportRow holds, in its
// order.
constexpr std::string_view kReportColumns =
    "reports.id, reports.file_id, reports.run_id, reports.bug_id, "
    "reports.line, reports.\"column\", reports.path_length, "
    "reports.checker_message, reports.detection_status, "
    "reports.review_status, reports.review_status_author, "
    "reports.review_status_message, reports.review_status_date, "
    "reports.review_status_is_in_source, reports.detected_at, "
    "reports.fixed_at, checkers.analyzer_name, checkers.checker_name, "
    "checkers.severity";

// The number of kReportColumns.
constexpr int kReportColumnCount = 19;

// The columns of a comment that ToCommentData() reads, in its order.
constexpr std::string_view kCommentColumns =
    "comments.id, comments.bug_hash, comments.author, comments.message, "
    "comments.created_at, comments.kind";

// A transaction on a connection. It is rolled back when the object is
// destroyed, unless Commit() was called, so a thrown RequestFailed leaves
// the database as it was (Python: DBSession).
class Transaction {
 public:
  // Begins the transaction on `connection`, which must outlive it.
  explicit Transaction(db::Connection* connection) : connection_(connection) {
    ThrowIfFailed(connection_->Execute("BEGIN"));
  }

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  ~Transaction() {
    if (!committed_) (void)connection_->Execute("ROLLBACK");
  }

  void Commit() {
    ThrowIfFailed(connection_->Execute("COMMIT"));
    committed_ = true;
  }

 private:
  db::Connection* connection_;
  bool committed_ = false;
};

// A row of kReportColumns.
struct ReportRow {
  int64_t id = 0;
  int64_t file_id = 0;
  int64_t run_id = 0;
  std::string bug_id;
  int64_t line = 0;
  int64_t column = 0;
  std::optional<int64_t> path_length;
  std::string checker_message;
  std::optional<std::string> detection_status;
  std::optional<std::string> review_status;
  std::optional<std::string> review_status_author;
  std::optional<std::string> review_status_message;
  std::optional<std::string> review_status_date;
  bool review_status_is_in_source = false;
  std::string detected_at;
  std::optional<std::string> fixed_at;
  std::string analyzer_name;
  std::string checker_name;
  int64_t severity = 0;
};

// Returns the report in the first kReportColumnCount columns of `row`.
ReportRow ToReportRow(const db::Row& row) {
  ReportRow report;
  report.id = row.Int(0);
  report.file_id = row.Int(1);
  report.run_id = row.Int(2);
  report.bug_id = row.Text(3);
  report.line = row.Int(4);
  report.column = row.Int(5);
  report.path_length = row.OptionalInt(6);
  report.checker_message = row.Text(7);
  report.detection_status = row.OptionalText(8);
  report.review_status = row.OptionalText(9);
  report.review_status_author = row.OptionalText(10);
  if (!row.IsNull(11)) {
    report.review_status_message = row.Blob(11);
  }
  report.review_status_date = row.OptionalText(12);
  report.review_status_is_in_source = row.Bool(13);
  report.detected_at = row.Text(14);
  report.fixed_at = row.OptionalText(15);
  report.analyzer_name = row.Text(16);
  report.checker_name = row.Text(17);
  report.severity = row.Int(18);
  return report;
}

// Returns the database time `time` as Python's str() writes it; "None" for
// NULL.
std::string PythonTime(const std::optional<std::string>& time) {
  return time ? util::DatabaseTimeToPython(*time) : "None";
}

// Returns the review data of a report or a rule (Python:
// create_review_data()).
ReviewData MakeReviewData(const std::optional<std::string>& status,
                          const std::optional<std::string>& message,
                          const std::optional<std::string>& author,
                          const std::optional<std::string>& date,
                          bool is_in_source) {
  ReviewData review;
  if (status) review.__set_status(ValueOrThrow(ReviewStatusOf(*status)));
  if (message) review.__set_comment(*message);
  if (author) review.__set_author(*author);
  if (date) review.__set_date(util::DatabaseTimeToPython(*date));
  review.__set_isInSource(is_in_source);
  return review;
}

// Returns the fields of ReportData that every call fills in the same way.
ReportData ToReportData(const ReportRow& report) {
  ReportData data;
  data.__set_runId(report.run_id);
  data.__set_bugHash(report.bug_id);
  data.__set_checkerMsg(report.checker_message);
  data.__set_reportId(report.id);
  data.__set_fileId(report.file_id);
  data.__set_line(report.line);
  data.__set_column(report.column);
  data.__set_analyzerName(report.analyzer_name);
  data.__set_checkerId(report.checker_name);
  data.__set_severity(static_cast<Severity::type>(report.severity));
  data.__set_reviewData(MakeReviewData(
      report.review_status, report.review_status_message,
      report.review_status_author, report.review_status_date,
      report.review_status_is_in_source));
  if (report.detection_status) {
    data.__set_detectionStatus(
        ValueOrThrow(DetectionStatusOf(*report.detection_status)));
  }
  data.__set_detectedAt(util::DatabaseTimeToPython(report.detected_at));
  return data;
}

// Returns `limit` capped at MAX_QUERY_SIZE, which is also the limit of 0
// (Python: verify_limit_range()).
int64_t VerifyLimitRange(int64_t limit) {
  const int64_t max_limit = g_report_server_constants.MAX_QUERY_SIZE;
  if (limit == 0) return max_limit;
  if (limit > max_limit) {
    kLog.Warning(std::format(
        "Query limit {} was larger than max query limit {}, setting limit "
        "to {}",
        limit, max_limit, max_limit));
    return max_limit;
  }
  return limit;
}

// Returns the text of the zlib-compressed `data`; throws GENERAL with
// Python's message if it is not zlib data.
std::string Decompress(std::string_view data) {
  return ValueOrThrow(util::ZlibDecompress(data), ErrorCode::GENERAL);
}

// Returns the text of RequestFailed(code, message) as Python's str() writes
// it.
std::string RequestFailedRepr(ErrorCode::type code,
                              const std::string& message) {
  return std::format("RequestFailed(errorCode={}, message={}, extraInfo=None)",
                     static_cast<int>(code), util::Repr(message));
}

// Returns `text` with its first character in upper case and the others in
// lower case (Python: str.capitalize() on the ASCII status names).
std::string Capitalize(std::string_view text) {
  std::string result = util::AsciiLower(text);
  if (!result.empty() && result[0] >= 'a' && result[0] <= 'z') {
    result[0] = static_cast<char>(result[0] - 'a' + 'A');
  }
  return result;
}

// Returns the URL of the commits of `remote_url` from the first pattern of
// `urls` that matches it; nullopt if none does (Python: get_commit_url()).
std::optional<std::string> CommitUrl(const std::optional<std::string>& remote,
                                     const std::vector<GitCommitUrl>& urls) {
  if (!remote || remote->empty()) return std::nullopt;
  for (const GitCommitUrl& pattern : urls) {
    const std::optional<util::RegexMatch> match = pattern.regex.Match(*remote);
    if (!match) continue;
    std::string url = pattern.url;
    for (const std::string& name : pattern.regex.GroupNames()) {
      if (const std::optional<std::string_view> value = match->Group(name)) {
        url = util::Replace(url, "$" + name, *value);
      }
    }
    return url;
  }
  return std::nullopt;
}

// Returns the part of `blame` at `line`: the blame ranges that contain it,
// and the commit of the first one (Python: __filter_blame_info_on_line()).
BlameInfo FilterBlameInfoOnLine(const BlameInfo& blame, int64_t line) {
  if (blame.commits.empty() || blame.blame.empty()) return blame;
  BlameInfo filtered;
  std::vector<BlameData> ranges;
  for (const BlameData& range : blame.blame) {
    if (range.startLine <= line && line <= range.endLine) {
      ranges.push_back(range);
    }
  }
  cc::api::OrderedMap<std::string, Commit> commits;
  if (!ranges.empty()) {
    const auto commit = blame.commits.find(ranges.front().commitHash);
    if (commit != blame.commits.end()) commits[commit->first] = commit->second;
  }
  filtered.__set_commits(commits);
  filtered.__set_blame(ranges);
  return filtered;
}

// A pivoted annotation column of getRunResults() (Python: an item of
// annotation_cols): the maximum of the values of the annotation `key` among
// the joined report_annotations rows.
struct AnnotationColumn {
  std::string key;
  db::Sql expression;
};

// Returns the comment in the kCommentColumns of `row` as the API sends it,
// with `message` as its text.
CommentData ToCommentData(const db::Row& row, const std::string& message) {
  CommentData comment;
  comment.__set_id(row.Int(0));
  comment.__set_author(row.Text(2));
  comment.__set_message(message);
  comment.__set_createdAt(util::DatabaseTimeToPython(row.Text(4)));
  comment.__set_kind(static_cast<CommentKind::type>(row.Int(5)));
  return comment;
}

// An item of the bug path data of a report: an event, a step of the path,
// or a note, macro expansion or fix-it (Python: ReportPathData.Item).
struct PathItem {
  int64_t from_row = 0;
  int64_t from_col = 0;
  int64_t to_row = 0;
  int64_t to_col = 0;
  int64_t file_id = 0;
  std::string type;
  std::optional<std::string> msg;
};

// Returns the items of the zlib-compressed JSON path data `data`.
std::vector<PathItem> ReadPathData(const std::string& data) {
  const util::JsonDocument document = ValueOrThrow(
      util::ParseJson(Decompress(data)), ErrorCode::GENERAL);
  std::vector<PathItem> items;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* item = nullptr;
  yyjson_arr_foreach(yyjson_doc_get_root(document.get()), index, count,
                     item) {
    auto number = [item](std::string_view key) {
      return util::JsonToInt(util::JsonGet(item, key)).value_or(0);
    };
    yyjson_val* msg = util::JsonGet(item, "msg");
    items.push_back(
        {.from_row = number("from_row"),
         .from_col = number("from_col"),
         .to_row = number("to_row"),
         .to_col = number("to_col"),
         .file_id = number("file_id"),
         .type = util::JsonString(util::JsonGet(item, "type")),
         .msg = yyjson_is_str(msg) ? std::optional<std::string>(
                                         util::JsonString(msg))
                                   : std::nullopt});
  }
  return items;
}

}  // namespace

db::Connection& ReportHandler::Db() {
  return *ValueOrThrow(request_.product->Connection());
}

db::QueryResult ReportHandler::Run(const db::Sql& sql) {
  return ValueOrThrow(Db().Query(sql.text(), sql.params()));
}

void ReportHandler::Require(
    std::initializer_list<Permission::type> permissions) {
  db::Connection* config_db = ValueOrThrow(request_.server->config_db().Get());
  const auth::Requester requester = {
      .session = request_.session,
      .authentication_enabled = request_.server->sessions().enabled()};
  for (const Permission::type permission : permissions) {
    if (ValueOrThrow(auth::RequirePermission(
            permission,
            {.config_db = config_db, .product_id = request_.product->id()},
            requester))) {
      return;
    }
  }
  throw MakeRequestFailed(ErrorCode::UNAUTHORIZED,
                          "You are not authorized to execute this action.");
}

std::string ReportHandler::User() const {
  return request_.session ? request_.session->user : "Anonymous";
}

bool ReportHandler::ClientAtLeast(int minor) const {
  const std::vector<std::string> parts =
      util::Split(request_.api_version, ".");
  // Routing accepts only versions 6.<minor>.
  return parts.size() >= 2 && util::ParseInt(parts[1]).value_or(0) >= minor;
}

std::string ReportHandler::CommentMessage(int64_t kind,
                                          const std::string& message) const {
  if (kind != kSystemComment) return util::HtmlEscape(message);
  Result<std::vector<std::string>> elements = util::ShlexSplit(message);
  if (!elements.ok()) {
    // Older servers stored system comments without quoting.
    elements = util::ShlexSplit(util::Replace(
        util::Replace(message, "'", "\\'"), "\"", "\\\""));
  }
  const std::vector<std::string> words =
      ValueOrThrow(std::move(elements), ErrorCode::GENERAL);
  if (words.empty()) {
    throw MakeRequestFailed(ErrorCode::GENERAL, "list index out of range");
  }
  for (const SystemCommentKind& kind :
       request_.server->package_data().system_comments) {
    if (kind.kind != words.front()) continue;
    std::string comment = kind.text;
    for (size_t i = 1; i < words.size(); ++i) {
      comment = util::Replace(comment, std::format("{{{}}}", i - 1),
                              util::HtmlEscape(words[i]));
    }
    return comment;
  }
  return util::HtmlEscape(message);
}

void ReportHandler::AddComment(const std::string& report_hash,
                               const std::string& message, int64_t kind,
                               int64_t date) {
  ValueOrThrow(Db().Query(
      "INSERT INTO comments (bug_hash, author, message, kind, created_at) "
      "VALUES (?, ?, ?, ?, ?)",
      {report_hash, User(), db::Bytes{message}, kind,
       util::FormatDatabaseTime(date)}));
}

std::map<int64_t, ReportDetails> ReportHandler::GetReportDetails(
    const std::vector<int64_t>& report_ids) {
  std::map<int64_t, std::vector<BugPathEvent>> events;
  std::map<int64_t, std::vector<BugPathPos>> points;
  std::map<int64_t, std::vector<ExtendedReportData>> extended;
  std::map<int64_t, std::string> files;
  db::Sql files_query(
      "SELECT files.id, files.filepath FROM files JOIN report_path_data_files "
      "ON report_path_data_files.file_id = files.id WHERE ");
  files_query.Add(
      db::In("report_path_data_files.report_path_data_id", report_ids));
  for (const db::Row& row : Run(files_query).rows) {
    files[row.Int(0)] = row.Text(1);
  }
  // Returns the path of the file `id`; throws GENERAL "<id>", Python's
  // KeyError, if the bug path has no such file.
  auto file_path = [&files](int64_t id) {
    const auto file = files.find(id);
    if (file == files.end()) {
      throw MakeRequestFailed(ErrorCode::GENERAL, std::to_string(id));
    }
    return file->second;
  };
  db::Sql path_query(
      "SELECT report_path_data.report_id, report_path_data.path_data "
      "FROM report_path_data WHERE ");
  path_query.Add(db::In("report_path_data.report_id", report_ids));
  for (const db::Row& row : Run(path_query).rows) {
    const int64_t report_id = row.Int(0);
    for (const PathItem& item : ReadPathData(row.Blob(1))) {
      if (item.type == "event") {
        BugPathEvent event;
        event.__set_startLine(item.from_row);
        event.__set_startCol(item.from_col);
        event.__set_endLine(item.to_row);
        event.__set_endCol(item.to_col);
        if (item.msg) event.__set_msg(*item.msg);
        event.__set_fileId(item.file_id);
        event.__set_filePath(file_path(item.file_id));
        events[report_id].push_back(std::move(event));
      } else if (item.type == "path") {
        BugPathPos point;
        point.__set_startLine(item.from_row);
        point.__set_startCol(item.from_col);
        point.__set_endLine(item.to_row);
        point.__set_endCol(item.to_col);
        point.__set_fileId(item.file_id);
        point.__set_filePath(file_path(item.file_id));
        points[report_id].push_back(std::move(point));
      } else {
        ExtendedReportData data;
        data.__set_type(ValueOrThrow(ExtendedReportDataTypeOf(item.type)));
        data.__set_startLine(item.from_row);
        data.__set_startCol(item.from_col);
        data.__set_endLine(item.to_row);
        data.__set_endCol(item.to_col);
        if (item.msg) data.__set_message(*item.msg);
        data.__set_fileId(item.file_id);
        data.__set_filePath(file_path(item.file_id));
        extended[report_id].push_back(std::move(data));
      }
    }
  }
  std::map<int64_t, std::vector<CommentData>> comments;
  db::Sql comment_query(std::format(
      "SELECT {}, reports.id FROM comments LEFT OUTER JOIN reports "
      "ON reports.bug_id = comments.bug_hash WHERE ",
      kCommentColumns));
  comment_query.Add(db::In("reports.id", report_ids))
      .Add(" ORDER BY comments.created_at DESC");
  for (const db::Row& row : Run(comment_query).rows) {
    comments[row.Int(6)].push_back(
        ToCommentData(row, CommentMessage(row.Int(5), row.Blob(3))));
  }
  std::map<int64_t, ReportDetails> details;
  for (const int64_t id : report_ids) {
    ReportDetails& report = details[id];
    report.__set_pathEvents(events[id]);
    report.__set_executionPath(points[id]);
    report.__set_extendedData(extended[id]);
    report.__set_comments(comments[id]);
  }
  return details;
}

void ReportHandler::SetReviewStatus(const std::string& report_hash,
                                    ReviewStatus::type status,
                                    const std::string& message,
                                    std::optional<int64_t> date) {
  db::Connection& db = Db();
  const db::QueryResult existing = ValueOrThrow(db.Query(
      "SELECT status, message FROM review_statuses WHERE bug_hash = ?",
      {report_hash}));
  std::string old_status = "unreviewed";
  std::optional<std::string> old_message;
  if (!existing.rows.empty()) {
    const db::Row& row = existing.rows.front();
    if (!row.Text(0).empty()) old_status = row.Text(0);
    if (!row.Blob(1).empty()) old_message = row.Blob(1);
  }
  const std::string new_status = ValueOrThrow(ReviewStatusName(status));
  // Parallel stores often set the same status again; only a change in the
  // status or the message counts.
  if (old_status == new_status && old_message == message) return;

  const int64_t when = date.value_or(util::LocalNow());
  const std::string when_text = util::FormatDatabaseTime(when);
  const std::string user = User();
  if (existing.rows.empty()) {
    ValueOrThrow(db.Query(
        "INSERT INTO review_statuses (bug_hash, status, author, message, "
        "date) VALUES (?, ?, ?, ?, ?)",
        {report_hash, new_status, user, db::Bytes{message}, when_text}));
  } else {
    ValueOrThrow(db.Query(
        "UPDATE review_statuses SET status = ?, author = ?, message = ?, "
        "date = ? WHERE bug_hash = ?",
        {new_status, user, db::Bytes{message}, when_text, report_hash}));
  }
  std::string comment =
      std::format("rev_st_changed {} {}", Capitalize(old_status),
                  Capitalize(new_status));
  if (!message.empty()) {
    comment = std::format("rev_st_changed_msg {} {} {}",
                          Capitalize(old_status), Capitalize(new_status),
                          util::ShlexQuote(message));
  }
  AddComment(report_hash, comment, kSystemComment, when);

  // False positive and intentional reports count as closed, so they get a
  // fix date; they are reopened when they become unreviewed or confirmed.
  if (new_status == "false_positive" || new_status == "intentional") {
    ValueOrThrow(db.Query(
        "UPDATE reports SET fixed_at = ? WHERE bug_id = ? AND "
        "detection_status IN ('unresolved', 'new', 'reopened') AND "
        "review_status NOT IN ('false_positive', 'intentional') AND "
        "review_status_is_in_source IS false",
        {when_text, report_hash}));
  } else {
    ValueOrThrow(db.Query(
        "UPDATE reports SET fixed_at = NULL WHERE id IN (SELECT reports.id "
        "FROM reports WHERE bug_id = ? AND detection_status IN "
        "('unresolved', 'new', 'reopened') AND review_status IN "
        "('false_positive', 'intentional')) AND "
        "review_status_is_in_source IS false",
        {report_hash}));
  }
  ValueOrThrow(db.Query(
      "UPDATE reports SET review_status = ?, review_status_author = ?, "
      "review_status_message = ?, review_status_date = ? WHERE "
      "review_status_is_in_source IS false AND bug_id = ?",
      {new_status, user, db::Bytes{message}, when_text, report_hash}));
}

Result<void> ReportHandler::CheckRemoveRunsLock(
    const std::vector<int64_t>& run_ids) {
  db::Select select = {
      .columns = db::Sql("run_locks.name"),
      .from = db::Sql(
          "run_locks LEFT OUTER JOIN runs ON runs.name = run_locks.name")};
  db::Sql recent("run_locks.locked_at >= ");
  select.where.push_back(recent.Bind(util::FormatDatabaseTime(
      util::LocalNow() -
      db::kRunLockTimeoutSeconds * util::kMicrosecondsPerSecond)));
  if (!run_ids.empty()) select.where.push_back(db::In("runs.id", run_ids));
  const db::Sql query = db::Build(select);
  Result<db::QueryResult> rows = Db().Query(query.text(), query.params());
  if (!rows.ok()) return Result<void>(rows.error());
  std::vector<std::string> locked;
  for (const db::Row& row : rows.value().rows) locked.push_back(row.Text(0));
  if (locked.empty()) return Result<void>();
  return Result<void>(Error{
      .message =
          "Can not remove results because the following runs are locked: " +
          util::Join(locked, ", ")});
}

Result<void> ReportHandler::ChangeCachedRunCount(int64_t change) {
  return SetCachedRunData(request_.server->config_db(),
                          request_.product->id(),
                          request_.product->endpoint(), change, std::nullopt);
}

db::Row ReportHandler::CleanupPlanRow(int64_t id) {
  const db::QueryResult rows = ValueOrThrow(Db().Query(
      "SELECT id, name, due_date, description, closed_at FROM cleanup_plans "
      "WHERE id = ?",
      {id}));
  if (rows.rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Cleanup plan '{}' was not found in the database.", id));
  }
  return rows.rows.front();
}

void ReportHandler::getRunData(RunDataList& result, const RunFilter& run_filter,
                               int64_t limit, int64_t offset,
                               const RunSortMode& sort_mode) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Select select = {
      .columns = db::Sql(
          "runs.id, runs.date, runs.name, runs.duration, "
          "run_histories.version_tag, run_histories.cc_version, "
          "run_histories.description, report_counts.report_count"),
      .from = db::Sql(
          "runs LEFT OUTER JOIN (SELECT reports.run_id AS run_id, "
          "count(reports.bug_id) AS report_count FROM reports WHERE "
          "reports.detection_status NOT IN ('resolved', 'off', "
          "'unavailable') AND reports.review_status NOT IN "
          "('false_positive', 'intentional') GROUP BY reports.run_id) "
          "AS report_counts ON runs.id = report_counts.run_id "
          "LEFT OUTER JOIN (SELECT run_histories.run_id AS run_id, "
          "max(run_histories.id) AS run_history_id, "
          "max(run_histories.time) AS run_history_time FROM run_histories "
          "GROUP BY run_histories.run_id) AS latest "
          "ON runs.id = latest.run_id LEFT OUTER JOIN run_histories "
          "ON run_histories.id = latest.run_history_id"),
      .group_by = {db::Sql(
          "runs.id, run_histories.version_tag, run_histories.cc_version, "
          "run_histories.description, report_counts.report_count")},
      .limit = VerifyLimitRange(limit),
      .offset = offset};
  ThrowIfFailed(ProcessRunFilter(Db(), run_filter, &select));
  if (!sort_mode.__isset.type && !sort_mode.__isset.ord) {
    select.order_by.emplace_back("runs.date DESC");
  } else {
    const std::string_view order =
        sort_mode.ord == Order::DESC ? " DESC" : " ASC";
    switch (sort_mode.type) {
      case RunSortType::NAME:
        select.order_by.emplace_back(std::format("runs.name{}", order));
        break;
      case RunSortType::UNRESOLVED_REPORTS:
        select.order_by.emplace_back(std::format("report_count{}", order));
        break;
      case RunSortType::DATE:
        select.order_by.emplace_back(std::format("runs.date{}", order));
        break;
      case RunSortType::DURATION:
        select.order_by.emplace_back(std::format("runs.duration{}", order));
        break;
      case RunSortType::CC_VERSION:
        select.order_by.emplace_back(
            std::format("run_histories.cc_version{}", order));
        break;
    }
  }
  const db::QueryResult runs = Run(db::Build(select));
  std::vector<int64_t> run_ids;
  for (const db::Row& row : runs.rows) run_ids.push_back(row.Int(0));

  std::map<int64_t, cc::api::OrderedMap<DetectionStatus::type, int32_t>>
      status_counts;
  db::Sql status_query(
      "SELECT reports.run_id, reports.detection_status, "
      "count(reports.bug_id) FROM reports WHERE ");
  status_query.Add(db::In("reports.run_id", run_ids))
      .Add(" GROUP BY reports.run_id, reports.detection_status");
  for (const db::Row& row : Run(status_query).rows) {
    if (row.IsNull(1)) continue;
    status_counts[row.Int(0)][ValueOrThrow(DetectionStatusOf(row.Text(1)))] =
        static_cast<int32_t>(row.Int(2));
  }
  std::map<int64_t, AnalyzerStatisticsData> statistics;
  if (!run_ids.empty()) {
    db::Sql statistics_query(
        "SELECT analyzer_statistics.analyzer_type, "
        "analyzer_statistics.failed, analyzer_statistics.successful, "
        "runs.id FROM analyzer_statistics LEFT OUTER JOIN run_histories "
        "ON run_histories.id = analyzer_statistics.run_history_id "
        "LEFT OUTER JOIN runs ON runs.id = run_histories.run_id WHERE "
        "analyzer_statistics.run_history_id IN (SELECT "
        "max(analyzer_statistics.run_history_id) FROM analyzer_statistics "
        "LEFT OUTER JOIN run_histories "
        "ON run_histories.id = analyzer_statistics.run_history_id WHERE ");
    statistics_query.Add(db::In("run_histories.run_id", run_ids))
        .Add(" GROUP BY run_histories.run_id)");
    for (const db::Row& row : Run(statistics_query).rows) {
      AnalyzerStatistics& analyzer = statistics[row.Int(3)][row.Text(0)];
      analyzer = AnalyzerStatistics();
      analyzer.__set_failed(row.Int(1));
      analyzer.__set_successful(row.Int(2));
    }
  }
  for (const db::Row& row : runs.rows) {
    const int64_t id = row.Int(0);
    RunData run;
    run.__set_runId(id);
    run.__set_runDate(PythonTime(row.OptionalText(1)));
    if (!row.IsNull(2)) run.__set_name(row.Text(2));
    if (!row.IsNull(3)) run.__set_duration(row.Int(3));
    run.__set_resultCount(row.Int(7));
    run.__set_detectionStatusCount(status_counts[id]);
    if (!row.IsNull(4)) run.__set_versionTag(row.Text(4));
    if (!row.IsNull(5)) run.__set_codeCheckerVersion(row.Text(5));
    run.__set_analyzerStatistics(statistics[id]);
    if (!row.IsNull(6)) run.__set_description(row.Text(6));
    result.push_back(std::move(run));
  }
}

int64_t ReportHandler::getRunCount(const RunFilter& run_filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Select select = {.columns = db::Sql("runs.id"),
                       .from = db::Sql("runs")};
  ThrowIfFailed(ProcessRunFilter(Db(), run_filter, &select));
  return Run(db::Count(db::Build(select))).rows.front().Int(0);
}

void ReportHandler::getCheckCommand(std::string& result,
                                    int64_t run_history_id, int64_t run_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  AnalysisInfoFilter filter;
  if (run_id != 0) filter.__set_runId(run_id);
  if (run_history_id != 0) filter.__set_runHistoryId(run_history_id);
  std::vector<AnalysisInfo> infos;
  getAnalysisInfo(infos, filter, /*limit=*/0, /*offset=*/0);
  std::vector<std::string> commands;
  for (const AnalysisInfo& info : infos) {
    commands.push_back(util::HtmlEscape(info.analyzerCommand));
  }
  result = util::Join(commands, "; ");
}

void ReportHandler::getAnalysisInfo(std::vector<AnalysisInfo>& result,
                                    const AnalysisInfoFilter& filter,
                                    int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const auto& set = filter.__isset;
  if (!set.runId && !set.runHistoryId && !set.reportId) return;
  db::Select select = {.columns = db::Sql(
                           "analysis_info.id, analysis_info.analyzer_command, "
                           "analysis_info.checker_set_id"),
                       .from = db::Sql("analysis_info")};
  if (set.reportId) {
    select.from.Add(
        " LEFT OUTER JOIN report_analysis_info "
        "ON report_analysis_info.analysis_info_id = analysis_info.id");
    select.where.push_back(db::In("report_analysis_info.report_id",
                                  std::vector<int64_t>{filter.reportId}));
  } else {
    select.from.Add(
        " LEFT OUTER JOIN run_history_analysis_info "
        "ON run_history_analysis_info.analysis_info_id = analysis_info.id");
    if (set.runId) {
      db::Sql latest(
          "SELECT run_histories.id FROM run_histories "
          "WHERE run_histories.run_id = ");
      latest.Bind(filter.runId)
          .Add(" ORDER BY run_histories.time DESC LIMIT 1");
      select.where.push_back(
          db::In("run_history_analysis_info.run_history_id", latest));
    } else {
      select.where.push_back(db::In("run_history_analysis_info.run_history_id",
                                    std::vector<int64_t>{
                                        filter.runHistoryId}));
    }
  }
  if (limit != 0) {
    select.limit = limit;
    select.offset = offset;
  }
  for (const db::Row& row : Run(db::Build(select)).rows) {
    std::string command;
    if (!row.Blob(1).empty()) {
      command = util::DecodeUtf8Ignore(Decompress(row.Blob(1)));
    }
    cc::api::OrderedMap<std::string,
                        cc::api::OrderedMap<std::string, AnalysisInfoChecker>>
        checkers;
    db::Sql checker_query(
        "SELECT checkers.analyzer_name, checkers.checker_name, "
        "checker_set_items.enabled FROM checker_set_items JOIN checkers "
        "ON checker_set_items.checker_id = checkers.id "
        "WHERE checker_set_items.checker_set_id ");
    if (row.IsNull(2)) {
      checker_query.Add("IS NULL");
    } else {
      checker_query.Add("= ").Bind(row.Int(2));
    }
    for (const db::Row& checker : Run(checker_query).rows) {
      AnalysisInfoChecker& info = checkers[checker.Text(0)][checker.Text(1)];
      info.__set_enabled(checker.Bool(2));
    }
    const db::QueryResult files = ValueOrThrow(Db().Query(
        "SELECT analysis_info_files.filename, file_contents.content "
        "FROM analysis_info_files JOIN file_contents "
        "ON analysis_info_files.content_hash = file_contents.content_hash "
        "WHERE analysis_info_files.analysis_info_id = ?",
        {row.Int(0)}));
    for (const db::Row& file : files.rows) {
      command += std::format("\n\n{}:\n", file.Text(0));
      command += util::DecodeUtf8Ignore(Decompress(file.Blob(1)));
    }
    AnalysisInfo info;
    info.__set_analyzerCommand(util::HtmlEscape(command));
    info.__set_checkers(checkers);
    result.push_back(std::move(info));
  }
}

void ReportHandler::getRunHistory(RunHistoryDataList& result,
                                  const std::vector<int64_t>& run_ids,
                                  int64_t limit, int64_t offset,
                                  const RunHistoryFilter& run_history_filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Select select = {
      .columns = db::Sql(
          "run_histories.id, run_histories.run_id, runs.name, "
          "run_histories.version_tag, run_histories.\"user\", "
          "run_histories.time, run_histories.cc_version, "
          "run_histories.description"),
      .from = db::Sql(
          "run_histories LEFT OUTER JOIN runs "
          "ON runs.id = run_histories.run_id"),
      .order_by = {db::Sql("run_histories.time DESC")},
      .limit = VerifyLimitRange(limit),
      .offset = offset};
  ProcessRunHistoryFilter(run_ids, run_history_filter, &select);
  const db::QueryResult histories = Run(db::Build(select));
  std::vector<int64_t> history_ids;
  for (const db::Row& row : histories.rows) history_ids.push_back(row.Int(0));
  std::map<int64_t, AnalyzerStatisticsData> statistics;
  db::Sql statistics_query(
      "SELECT run_history_id, analyzer_type, failed, successful "
      "FROM analyzer_statistics WHERE ");
  statistics_query.Add(db::In("run_history_id", history_ids));
  for (const db::Row& row : Run(statistics_query).rows) {
    AnalyzerStatistics& analyzer = statistics[row.Int(0)][row.Text(1)];
    analyzer = AnalyzerStatistics();
    analyzer.__set_failed(row.Int(2));
    analyzer.__set_successful(row.Int(3));
  }
  for (const db::Row& row : histories.rows) {
    RunHistoryData history;
    history.__set_id(row.Int(0));
    history.__set_runId(row.Int(1));
    history.__set_runName(row.Text(2));
    if (!row.IsNull(3)) history.__set_versionTag(row.Text(3));
    history.__set_user(row.Text(4));
    history.__set_time(util::DatabaseTimeToPython(row.Text(5)));
    if (!row.IsNull(6)) history.__set_codeCheckerVersion(row.Text(6));
    history.__set_analyzerStatistics(statistics[row.Int(0)]);
    if (!row.IsNull(7)) history.__set_description(row.Text(7));
    result.push_back(std::move(history));
  }
}

int64_t ReportHandler::getRunHistoryCount(
    const std::vector<int64_t>& run_ids,
    const RunHistoryFilter& run_history_filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Select select = {.columns = db::Sql("run_histories.id"),
                       .from = db::Sql("run_histories")};
  ProcessRunHistoryFilter(run_ids, run_history_filter, &select);
  return Run(db::Count(db::Build(select))).rows.front().Int(0);
}

void ReportHandler::getReport(ReportData& result, int64_t report_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::QueryResult rows = ValueOrThrow(Db().Query(
      std::format("SELECT {}, files.filepath FROM reports JOIN checkers "
                  "ON reports.checker_id = checkers.id LEFT OUTER JOIN files "
                  "ON reports.file_id = files.id WHERE reports.id = ? "
                  "LIMIT 1",
                  kReportColumns),
      {report_id}));
  if (rows.rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Report {} not found!", report_id));
  }
  const db::Row& row = rows.rows.front();
  const ReportRow report = ToReportRow(row);
  result = ToReportData(report);
  result.__set_checkedFile(row.Text(kReportColumnCount));
  if (report.fixed_at) {
    result.__set_fixedAt(util::DatabaseTimeToPython(*report.fixed_at));
  }
}

void ReportHandler::getDiffResultsHash(
    std::vector<std::string>& result, const std::vector<int64_t>& run_ids,
    const std::vector<std::string>& report_hashes, DiffType::type diff_type,
    const std::vector<DetectionStatus::type>& skip_detection_statuses,
    const std::vector<int64_t>& tag_ids) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  // Clients before API 6.50 expect false positive and intentional reports
  // not to count as closed (Python: the client_version branches).
  const bool new_client = ClientAtLeast(50);
  const std::vector<std::string> skip_statuses =
      ValueOrThrow(DetectionStatusNames(
          skip_detection_statuses.empty()
              ? std::vector<DetectionStatus::type>{DetectionStatus::RESOLVED,
                                                   DetectionStatus::OFF,
                                                   DetectionStatus::UNAVAILABLE}
              : skip_detection_statuses));
  // Adds the conditions of the runs and the tags to `select` (Python:
  // filter_open_reports_in_tags() and its _old() variant).
  auto in_runs_and_tags = [&run_ids, &tag_ids, new_client](db::Select* select) {
    if (!run_ids.empty()) {
      select->where.push_back(db::In("reports.run_id", run_ids));
    }
    if (!tag_ids.empty()) {
      select->from.Add(
          " LEFT OUTER JOIN run_histories "
          "ON run_histories.run_id = reports.run_id");
      select->where.push_back(db::In("run_histories.id", tag_ids));
      select->where.push_back(
          new_client ? OpenReportsAt("reports", db::Sql("run_histories.time"))
                     : db::Sql("reports.detected_at <= run_histories.time"));
    }
  };
  db::Select select = {.columns = db::Sql("reports.bug_id"),
                       .from = db::Sql("reports")};
  switch (diff_type) {
    case DiffType::NEW: {
      // The hashes of `report_hashes` that are not among the open reports.
      // The reference subtracts the reports with EXCEPT; here the hashes
      // found are looked up in chunks.
      select.distinct = true;
      select.where.push_back(
          db::NotIn("reports.detection_status", skip_statuses));
      if (new_client) select.where.emplace_back("reports.fixed_at IS NULL");
      in_runs_and_tags(&select);
      std::set<std::string> found;
      for (const std::vector<std::string>& chunk :
           db::Chunks(report_hashes)) {
        db::Select chunk_select = select;
        chunk_select.where.push_back(db::In("reports.bug_id", chunk));
        for (const db::Row& row : Run(db::Build(chunk_select)).rows) {
          found.insert(row.Text(0));
        }
      }
      const std::set<std::string> hashes(report_hashes.begin(),
                                         report_hashes.end());
      std::ranges::set_difference(hashes, found, std::back_inserter(result));
      return;
    }
    case DiffType::RESOLVED:
      if (new_client) {
        db::Sql gone = db::NotIn("reports.bug_id", report_hashes);
        gone.Add(" OR reports.fixed_at IS NOT NULL");
        select.where.push_back(db::And({gone}));
      } else {
        select.where.push_back(db::NotIn("reports.bug_id", report_hashes));
      }
      break;
    case DiffType::UNRESOLVED:
      select.where.push_back(db::In("reports.bug_id", report_hashes));
      select.where.push_back(
          db::NotIn("reports.detection_status", skip_statuses));
      if (new_client) select.where.emplace_back("reports.fixed_at IS NULL");
      break;
    default:
      return;
  }
  in_runs_and_tags(&select);
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result.push_back(row.Text(0));
  }
}

namespace {

// Returns the pivoted annotation columns of the product's annotation keys
// (Python: the annotation_cols of getRunResults()). Only the keys that a
// store accepts have a type; others fail with Python's KeyError.
std::vector<AnnotationColumn> LoadAnnotationColumns(db::Connection& db) {
  std::vector<AnnotationColumn> columns;
  const db::QueryResult keys = ValueOrThrow(db.Query(
      "SELECT DISTINCT report_annotations.\"key\" FROM report_annotations",
      {}));
  for (const db::Row& row : keys.rows) {
    const std::string key = row.Text(0);
    // Python: report_annotation_types.
    std::string_view type;
    if (key == "timestamp" || key == "testcase") {
      type = "VARCHAR";
    } else if (key == "chronological_order") {
      type = "INTEGER";
    } else {
      throw MakeRequestFailed(ErrorCode::GENERAL, util::Repr(key));
    }
    db::Sql expression(
        "max(CASE WHEN report_annotations.\"key\" = ");
    expression.Bind(key).Add(
        std::format(" THEN CAST(report_annotations.value AS {}) END)", type));
    columns.push_back({.key = key, .expression = std::move(expression)});
  }
  return columns;
}

// Returns the expression of the column of `key` among `columns`; throws
// GENERAL "'<key>'", Python's KeyError, if there is none.
const db::Sql& AnnotationExpression(
    const std::vector<AnnotationColumn>& columns, const std::string& key) {
  const auto found = std::ranges::find(columns, key, &AnnotationColumn::key);
  if (found == columns.end()) {
    throw MakeRequestFailed(ErrorCode::GENERAL, util::Repr(key));
  }
  return found->expression;
}

// Returns the SELECT list items of `columns`, labeled "annotation_<key>".
db::Sql AnnotationSelectList(const std::vector<AnnotationColumn>& columns) {
  db::Sql list;
  for (const AnnotationColumn& column : columns) {
    list.Add(", ").Add(column.expression).Add(
        std::format(" AS \"annotation_{}\"", column.key));
  }
  return list;
}

// Returns the HAVING condition of the annotation filter `annotations` on
// the pivoted `columns`: any key with any of its values (Python: the
// having() part of getRunResults()).
db::Sql AnnotationHaving(const std::vector<Pair>& annotations,
                         const std::vector<AnnotationColumn>& columns) {
  std::vector<db::Sql> any;
  for (const Pair& annotation : annotations) {
    any.push_back(db::ILike(AnnotationExpression(columns, annotation.first),
                            Conv(annotation.second)));
  }
  return db::Or(any);
}

// Returns the ORDER BY items of `sort_types`; by severity, descending, if it
// is empty (Python: get_sort_map() and sort_results_query()). `unique`
// sorts by file name instead of path, and not by detection status.
std::vector<db::Sql> SortColumns(const std::vector<SortMode>& sort_types,
                                 bool unique) {
  std::vector<SortMode> modes = sort_types;
  if (modes.empty()) {
    modes.emplace_back();
    modes.back().__set_type(SortType::SEVERITY);
    modes.back().__set_ord(Order::DESC);
  }
  std::vector<db::Sql> order_by;
  for (const SortMode& mode : modes) {
    std::vector<std::string_view> columns;
    switch (mode.type) {
      case SortType::FILENAME:
        columns = unique ? std::vector<std::string_view>{"files.filename"}
                         : std::vector<std::string_view>{"files.filepath",
                                                         "reports.line"};
        break;
      case SortType::BUG_PATH_LENGTH:
        columns = {"reports.path_length"};
        break;
      case SortType::CHECKER_NAME:
        columns = {"checkers.checker_name"};
        break;
      case SortType::SEVERITY:
        columns = {"checkers.severity"};
        break;
      case SortType::REVIEW_STATUS:
        columns = {"reports.review_status"};
        break;
      case SortType::DETECTION_STATUS:
        if (!unique) columns = {"reports.detection_status"};
        break;
      case SortType::TIMESTAMP:
        columns = {"annotation_timestamp"};
        break;
      case SortType::TESTCASE:
        columns = {"annotation_testcase"};
        break;
      case SortType::CHRONOLOGICAL_ORDER:
        columns = {"annotation_chronological_order"};
        break;
      default:
        throw MakeRequestFailed(ErrorCode::GENERAL,
                                "'NoneType' object is not iterable");
    }
    for (const std::string_view column : columns) {
      if (mode.ord != Order::ASC && mode.ord != Order::DESC) {
        throw MakeRequestFailed(ErrorCode::GENERAL,
                                "'NoneType' object is not callable");
      }
      order_by.emplace_back(std::format(
          "{} {}", column, mode.ord == Order::ASC ? "ASC" : "DESC"));
    }
  }
  return order_by;
}

// Adds the outer join of the report annotations to `select`, grouped by
// report, as the count queries do when the filter has annotations.
void JoinAnnotations(db::Select* select) {
  select->from.Add(
      " LEFT OUTER JOIN report_annotations "
      "ON report_annotations.report_id = reports.id");
  select->group_by.emplace_back("reports.id");
}

}  // namespace

void ReportHandler::getRunResults(ReportDataList& result,
                                  const std::vector<int64_t>& run_ids,
                                  int64_t limit, int64_t offset,
                                  const std::vector<SortMode>& sort_types,
                                  const ReportFilter& report_filter,
                                  const CompareData& cmp_data,
                                  bool get_details) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  db::Connection& db = Db();
  const std::vector<AnnotationColumn> annotations = LoadAnnotationColumns(db);
  const bool unique = report_filter.isUnique;
  // The annotation filter is a HAVING on the pivoted annotations. In the
  // unique mode it is left out of the WHERE clause.
  ReportFilter where_filter = report_filter;
  if (unique) where_filter.__isset.annotations = false;
  const ReportFilterSql filter = ValueOrThrow(ProcessReportFilter(
      db, run_ids, where_filter, &cmp_data,
      /*keep_all_annotations=*/!unique));
  const std::string_view file_column =
      unique ? "files.filename" : "files.filepath";
  db::Sql columns(std::format("{}, {}", kReportColumns, file_column));
  if (unique) {
    columns.Add(
        ", row_number() OVER (PARTITION BY reports.bug_id "
        "ORDER BY reports.id DESC) AS row_num");
  }
  columns.Add(AnnotationSelectList(annotations));
  db::Select select = {
      .columns = columns,
      .from = db::Sql(
          "reports JOIN checkers ON reports.checker_id = checkers.id "
          "LEFT OUTER JOIN files ON reports.file_id = files.id "
          "LEFT OUTER JOIN report_annotations "
          "ON reports.id = report_annotations.report_id"),
      .group_by = {db::Sql("reports.id, files.id, checkers.id")}};
  ApplyReportFilter(filter, {.checkers = true, .files = true}, &select);
  if (!report_filter.annotations.empty()) {
    select.having.push_back(
        AnnotationHaving(report_filter.annotations, annotations));
  }
  select.order_by = SortColumns(sort_types, unique);
  db::Sql query;
  if (unique) {
    query.Add("SELECT * FROM (")
        .Add(db::Build(select))
        .Add(std::format(") AS unique_reports WHERE "
                         "unique_reports.row_num = 1 LIMIT {}",
                         limit));
    if (offset > 0) query.Add(std::format(" OFFSET {}", offset));
  } else {
    // Without a full order, pages of reports could overlap.
    select.order_by.emplace_back("reports.id");
    select.limit = limit;
    select.offset = offset;
    query = db::Build(select);
  }
  const db::QueryResult rows = Run(query);
  // The first annotation column.
  const int first_annotation = kReportColumnCount + (unique ? 2 : 1);

  std::map<int64_t, ReportDetails> details;
  std::map<int64_t, BlameInfo> blame_infos;
  if (get_details && !rows.rows.empty()) {
    std::vector<int64_t> report_ids;
    for (const db::Row& row : rows.rows) {
      const ReportRow report = ToReportRow(row);
      report_ids.push_back(report.id);
      getBlameInfo(blame_infos[report.id], report.file_id);
    }
    details = GetReportDetails(report_ids);
  }
  for (const db::Row& row : rows.rows) {
    const ReportRow report = ToReportRow(row);
    ReportData data = ToReportData(report);
    data.__set_checkedFile(row.Text(kReportColumnCount));
    cc::api::OrderedMap<std::string, std::string> values;
    if (unique) {
      // As in the reference, the keys are paired with the testcase and the
      // timestamp, in this order, whichever keys the product has.
      std::vector<std::optional<std::string>> pivoted;
      for (const std::string_view key : {"testcase", "timestamp"}) {
        const auto found =
            std::ranges::find(annotations, key, &AnnotationColumn::key);
        pivoted.push_back(
            found == annotations.end()
                ? std::nullopt
                : row.OptionalText(first_annotation +
                                   static_cast<int>(
                                       found - annotations.begin())));
      }
      for (size_t i = 0; i < annotations.size() && i < pivoted.size(); ++i) {
        if (pivoted[i]) values[annotations[i].key] = *pivoted[i];
      }
      data.__set_fixedAt(PythonTime(report.fixed_at));
    } else {
      for (size_t i = 0; i < annotations.size(); ++i) {
        const std::optional<std::string> value =
            row.OptionalText(first_annotation + static_cast<int>(i));
        if (value) values[annotations[i].key] = *value;
      }
      if (report.fixed_at) {
        data.__set_fixedAt(util::DatabaseTimeToPython(*report.fixed_at));
      }
    }
    if (report.path_length) data.__set_bugPathLength(*report.path_length);
    if (const auto found = details.find(report.id); found != details.end()) {
      data.__set_details(found->second);
    }
    if (const auto found = blame_infos.find(report.id);
        found != blame_infos.end()) {
      data.__set_blameInfo(FilterBlameInfoOnLine(found->second, report.line));
    }
    data.__set_annotations(values);
    result.push_back(std::move(data));
  }
}

void ReportHandler::getReportAnnotations(std::vector<std::string>& result,
                                         const std::vector<int64_t>& run_ids,
                                         const ReportFilter& report_filter,
                                         const CompareData& cmp_data) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select reports = {.columns = db::Sql("reports.id"),
                        .from = db::Sql("reports")};
  ApplyReportFilter(filter, {}, &reports);
  db::Sql query;
  if (report_filter.__isset.annotations) {
    reports.from.Add(
        " LEFT OUTER JOIN report_annotations "
        "ON report_annotations.report_id = reports.id");
    reports.columns.Add(
        ", report_annotations.\"key\" AS annotations_key, "
        "report_annotations.value AS annotations_value");
    reports.group_by = {db::Sql(
        "reports.id, report_annotations.\"key\", report_annotations.value")};
    // As in the reference, a report must have every key of the filter.
    std::vector<db::Sql> keys;
    for (const Pair& annotation : report_filter.annotations) {
      db::Sql key("extended.annotations_key = ");
      keys.push_back(key.Bind(annotation.first));
    }
    query.Add("SELECT DISTINCT extended.annotations_value FROM (")
        .Add(db::Build(reports))
        .Add(") AS extended");
    if (!keys.empty()) query.Add(" WHERE ").Add(db::Join(keys, " AND "));
  } else {
    query
        .Add("SELECT DISTINCT report_annotations.value FROM "
             "report_annotations JOIN (")
        .Add(db::Build(reports))
        .Add(") AS extended ON report_annotations.report_id = extended.id");
  }
  for (const db::Row& row : Run(query).rows) result.push_back(row.Text(0));
}

void ReportHandler::getRunReportCounts(RunReportCounts& result,
                                       const std::vector<int64_t>& run_ids,
                                       const ReportFilter& report_filter,
                                       int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, nullptr));
  db::Select reports = {.columns = db::Sql("reports.bug_id, reports.run_id"),
                        .from = db::Sql("reports")};
  ApplyReportFilter(filter, {}, &reports);
  if (report_filter.__isset.annotations) JoinAnnotations(&reports);
  db::Select select = {
      .columns = db::Sql(std::format(
          "runs.id, max(runs.name), {}",
          report_filter.isUnique ? "count(DISTINCT filtered.bug_id)"
                                 : "count(*)")),
      .from = db::Sql("("),
      .group_by = {db::Sql("runs.id")},
      .order_by = {db::Sql("runs.name")},
      .limit = VerifyLimitRange(limit),
      .offset = offset};
  select.from.Add(db::Build(reports))
      .Add(") AS filtered JOIN runs ON runs.id = filtered.run_id");
  for (const db::Row& row : Run(db::Build(select)).rows) {
    RunReportCount count;
    count.__set_runId(row.Int(0));
    count.__set_name(row.Text(1));
    count.__set_reportCount(row.Int(2));
    result.push_back(std::move(count));
  }
}

int64_t ReportHandler::getRunResultCount(const std::vector<int64_t>& run_ids,
                                         const ReportFilter& report_filter,
                                         const CompareData& cmp_data) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select select = {.columns = db::Sql("reports.bug_id"),
                       .distinct = report_filter.isUnique,
                       .from = db::Sql("reports")};
  if (report_filter.__isset.annotations) JoinAnnotations(&select);
  ApplyReportFilter(filter, {}, &select);
  return Run(db::Count(db::Build(select))).rows.front().Int(0);
}

void ReportHandler::getReportDetails(ReportDetails& result,
                                     int64_t report_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  result = GetReportDetails({report_id})[report_id];
}

namespace {

// The query of a count call: the filtered reports with the `columns` they
// count, as the subquery "extended" (Python: extended_table).
db::Select ExtendedTable(const db::Sql& columns, bool join_checkers,
                         const ReportFilter& report_filter,
                         const ReportFilterSql& filter) {
  db::Select select = {.columns = columns, .from = db::Sql("reports")};
  if (join_checkers) {
    select.from.Add(" JOIN checkers ON reports.checker_id = checkers.id");
  }
  if (report_filter.__isset.annotations) JoinAnnotations(&select);
  ApplyReportFilter(filter, {.checkers = join_checkers}, &select);
  return select;
}

// Returns "(<extended>) AS extended" for a FROM clause.
db::Sql FromExtended(const db::Select& extended) {
  db::Sql from("(");
  return from.Add(db::Build(extended)).Add(") AS extended");
}

// Returns the query that counts the reports of `extended` by the value of
// its column `column` (Python: the counting part of getCheckerMsgCounts(),
// getAnalyzerNameCounts() and getSeverityCounts()). In the unique mode,
// each report hash counts once, with the largest value among its reports.
// With `ordered`, the result is ordered by the value.
db::Select CountByColumn(std::string_view column, bool unique, bool ordered,
                         const db::Select& extended) {
  db::Select select;
  if (unique) {
    db::Select per_hash = {
        .columns = db::Sql(std::format(
            "max(extended.{0}) AS {0}, extended.bug_id", column)),
        .from = FromExtended(extended),
        .group_by = {db::Sql("extended.bug_id")}};
    select.columns = db::Sql(std::format("hashes.{}, count(hashes.bug_id)",
                                         column));
    select.from = db::Sql("(");
    select.from.Add(db::Build(per_hash)).Add(") AS hashes");
    select.group_by.emplace_back(std::format("hashes.{}", column));
    if (ordered) select.order_by.emplace_back(std::format("hashes.{}", column));
  } else {
    select.columns = db::Sql(std::format("extended.{}, count(*)", column));
    select.from = FromExtended(extended);
    select.group_by.emplace_back(std::format("extended.{}", column));
    if (ordered) {
      select.order_by.emplace_back(std::format("extended.{}", column));
    }
  }
  return select;
}

}  // namespace

void ReportHandler::getCheckerCounts(CheckerCounts& result,
                                     const std::vector<int64_t>& run_ids,
                                     const ReportFilter& report_filter,
                                     const CompareData& cmp_data,
                                     int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  const db::Select extended = ExtendedTable(
      db::Sql("reports.bug_id, checkers.checker_name, checkers.severity"),
      /*join_checkers=*/true, report_filter, filter);
  db::Select select;
  if (report_filter.isUnique) {
    db::Select per_hash = {
        .columns = db::Sql(
            "max(extended.checker_name) AS checker_name, "
            "max(extended.severity) AS severity, extended.bug_id"),
        .from = FromExtended(extended),
        .group_by = {db::Sql("extended.bug_id")}};
    select = {.columns = db::Sql(
                  "hashes.checker_name, max(hashes.severity), "
                  "count(hashes.bug_id)"),
              .from = db::Sql("("),
              .group_by = {db::Sql("hashes.checker_name")},
              .order_by = {db::Sql("hashes.checker_name")}};
    select.from.Add(db::Build(per_hash)).Add(") AS hashes");
  } else {
    select = {.columns = db::Sql(
                  "extended.checker_name, extended.severity, count(*)"),
              .from = FromExtended(extended),
              .group_by = {db::Sql("extended.checker_name, "
                                   "extended.severity")},
              .order_by = {db::Sql("extended.checker_name")}};
  }
  select.limit = limit;
  select.offset = offset;
  for (const db::Row& row : Run(db::Build(select)).rows) {
    CheckerCount count;
    count.__set_name(row.Text(0));
    count.__set_severity(static_cast<Severity::type>(row.Int(1)));
    count.__set_count(row.Int(2));
    result.push_back(std::move(count));
  }
}

void ReportHandler::getCheckerStatusVerificationDetails(
    CheckerStatusVerificationDetails& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Connection& db = Db();
  const db::Sql in_runs = run_ids.empty()
                              ? db::Sql("true")
                              : db::In("run_histories.run_id", run_ids);
  std::map<int64_t, std::set<int64_t>> enabled;
  std::map<int64_t, std::set<int64_t>> disabled;
  for (const db::Row& row :
       Run(db::Sql("SELECT checkers.id, checkers.checker_name, "
                   "checkers.severity, checkers.analyzer_name "
                   "FROM checkers"))
           .rows) {
    CheckerStatusVerificationDetail& detail = result[row.Int(0)];
    detail.__set_checkerName(row.Text(1));
    detail.__set_analyzerName(row.Text(3));
    detail.__set_severity(static_cast<Severity::type>(row.Int(2)));
    detail.__set_closed(0);
    detail.__set_outstanding(0);
  }
  // Whether each checker was enabled in the latest analysis of each run.
  // Runs stored by older clients have no checker list: their checkers are
  // "unknown".
  const std::string_view run_list =
      db.dialect() == db::Dialect::kPostgresql
          ? "string_agg(DISTINCT CAST(runs.id AS VARCHAR), ',')"
          : "group_concat(DISTINCT runs.id)";
  db::Sql status_query(std::format(
      "SELECT checkers.id, {}, checker_set_items.enabled FROM (SELECT "
      "run_histories.run_id AS run_id, max(run_histories.id) AS "
      "max_run_history_id FROM run_histories WHERE ",
      run_list));
  status_query.Add(in_runs)
      .Add(
          " GROUP BY run_histories.run_id) AS latest, runs JOIN "
          "run_histories ON run_histories.run_id = runs.id LEFT OUTER JOIN "
          "run_history_analysis_info ON run_histories.id = "
          "run_history_analysis_info.run_history_id LEFT OUTER JOIN "
          "analysis_info ON analysis_info.id = "
          "run_history_analysis_info.analysis_info_id LEFT OUTER JOIN "
          "checker_set_items ON analysis_info.checker_set_id = "
          "checker_set_items.checker_set_id LEFT OUTER JOIN checkers "
          "ON checkers.id = checker_set_items.checker_id WHERE "
          "run_histories.id = latest.max_run_history_id "
          "GROUP BY checkers.id, checker_set_items.enabled");
  std::set<int64_t> unknown;
  for (const db::Row& row : Run(status_query).rows) {
    std::set<int64_t> runs;
    for (const std::string& id : util::Split(row.Text(1), ",")) {
      runs.insert(util::ParseInt(id).value_or(0));
    }
    if (row.IsNull(0) || row.Int(0) == 0) {
      unknown = std::move(runs);
    } else if (row.Bool(2)) {
      enabled[row.Int(0)].insert(runs.begin(), runs.end());
    } else {
      disabled[row.Int(0)].insert(runs.begin(), runs.end());
    }
  }
  for (auto& [id, detail] : result) {
    detail.__set_unknown(std::vector<int64_t>(unknown.begin(), unknown.end()));
    detail.__set_enabled(
        std::vector<int64_t>(enabled[id].begin(), enabled[id].end()));
    detail.__set_disabled(
        std::vector<int64_t>(disabled[id].begin(), disabled[id].end()));
  }
  db::Sql count_query(std::format(
      "SELECT checkers.id, {} AS report_count, CASE WHEN reports.fixed_at "
      "IS NULL THEN 1 ELSE 0 END AS is_open_case FROM reports JOIN "
      "checkers ON reports.checker_id = checkers.id WHERE ",
      report_filter.isUnique ? "count(DISTINCT reports.bug_id)"
                             : "count(reports.bug_id)"));
  count_query
      .Add(run_ids.empty() ? db::Sql("true")
                           : db::In("reports.run_id", run_ids))
      .Add(" GROUP BY checkers.id, is_open_case");
  for (const db::Row& row : Run(count_query).rows) {
    CheckerStatusVerificationDetail& detail = result[row.Int(0)];
    if (row.Bool(2)) {
      detail.__set_outstanding(row.Int(1));
    } else {
      detail.__set_closed(row.Int(1));
    }
  }
}

void ReportHandler::getAnalyzerNameCounts(
    cc::api::OrderedMap<std::string, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data, int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select select = CountByColumn(
      "analyzer_name", report_filter.isUnique, /*ordered=*/true,
      ExtendedTable(db::Sql("checkers.analyzer_name, reports.bug_id"),
                    /*join_checkers=*/true, report_filter, filter));
  select.limit = limit;
  select.offset = offset;
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result[row.Text(0)] = row.Int(1);
  }
}

void ReportHandler::getSeverityCounts(
    cc::api::OrderedMap<Severity::type, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  const db::Select select = CountByColumn(
      "severity", report_filter.isUnique, /*ordered=*/false,
      ExtendedTable(db::Sql("reports.bug_id, checkers.severity"),
                    /*join_checkers=*/true, report_filter, filter));
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result[static_cast<Severity::type>(row.Int(0))] = row.Int(1);
  }
}

void ReportHandler::getCheckerMsgCounts(
    cc::api::OrderedMap<std::string, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data, int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select select = CountByColumn(
      "checker_message", report_filter.isUnique, /*ordered=*/true,
      ExtendedTable(db::Sql("reports.checker_message, reports.bug_id"),
                    /*join_checkers=*/false, report_filter, filter));
  select.limit = limit;
  select.offset = offset;
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result[row.Text(0)] = row.Int(1);
  }
}

void ReportHandler::getReportStatusCounts(
    cc::api::OrderedMap<ReportStatus::type, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Sql columns(
      "reports.review_status, reports.detection_status, reports.bug_id, ");
  columns.Add(OutstandingCondition()).Add(" AS \"isOutstanding\"");
  const db::Select extended = ExtendedTable(columns, /*join_checkers=*/false,
                                            report_filter, filter);
  const db::Select select = {
      .columns = db::Sql(std::format(
          "extended.\"isOutstanding\", count({}extended.bug_id)",
          report_filter.isUnique ? "DISTINCT " : "")),
      .from = FromExtended(extended),
      .group_by = {db::Sql("extended.\"isOutstanding\"")}};
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result[row.Bool(0) ? ReportStatus::OUTSTANDING : ReportStatus::CLOSED] =
        row.Int(1);
  }
}

void ReportHandler::getReviewStatusCounts(
    cc::api::OrderedMap<ReviewStatus::type, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  const db::Select select = {
      .columns = db::Sql(std::format(
          "extended.review_status, count({}extended.bug_id)",
          report_filter.isUnique ? "DISTINCT " : "")),
      .from = FromExtended(
          ExtendedTable(db::Sql("reports.review_status, reports.bug_id"),
                        /*join_checkers=*/false, report_filter, filter)),
      .group_by = {db::Sql("extended.review_status")}};
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result[ValueOrThrow(ReviewStatusOf(row.Text(0)))] = row.Int(1);
  }
}

void ReportHandler::getDetectionStatusCounts(
    cc::api::OrderedMap<DetectionStatus::type, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  const db::Select select = {
      .columns = db::Sql(report_filter.isUnique
                             ? "extended.detection_status, "
                               "count(DISTINCT extended.bug_id)"
                             : "extended.detection_status, count(*)"),
      .from = FromExtended(
          ExtendedTable(db::Sql("reports.detection_status, reports.bug_id"),
                        /*join_checkers=*/false, report_filter, filter)),
      .group_by = {db::Sql("extended.detection_status")}};
  for (const db::Row& row : Run(db::Build(select)).rows) {
    if (row.IsNull(0)) continue;
    result[ValueOrThrow(DetectionStatusOf(row.Text(0)))] = row.Int(1);
  }
}

namespace {

// Returns the query of the paths of the files with reports that `filter`
// selects, one page of `limit` from `offset` (Python: distinct_file_path of
// getFileCounts()).
db::Sql DistinctFilePaths(const ReportFilter& report_filter,
                          const ReportFilterSql& filter, int64_t limit,
                          int64_t offset) {
  db::Select select = {
      .columns = db::Sql("files.filepath"),
      .distinct = true,
      .from = db::Sql("files JOIN reports ON reports.file_id = files.id"),
      .limit = limit,
      .offset = offset};
  if (report_filter.__isset.annotations) JoinAnnotations(&select);
  ApplyReportFilter(filter, {.files = true}, &select);
  return db::Build(select);
}

// Adds `filter` to the count query `select` of getFileCounts(). As in the
// reference, an annotation filter adds the report_annotations table without
// a join condition.
void ApplyFileCountFilter(const ReportFilterSql& filter,
                          const JoinedTables& joined, db::Select* select) {
  ApplyReportFilter(filter, joined, select);
  if (filter.uses_annotations) select->from.Add(", report_annotations");
}

}  // namespace

void ReportHandler::getFileCounts(
    cc::api::OrderedMap<std::string, int64_t>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data, int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select select = {
      .columns = db::Sql(std::format(
          "files.filepath, count({}reports.bug_id) AS report_num",
          report_filter.isUnique ? "DISTINCT " : "")),
      .from = db::Sql("files JOIN reports ON reports.file_id = files.id"),
      .where = {db::In("files.filepath",
                       DistinctFilePaths(report_filter, filter, limit,
                                         offset))},
      .group_by = {db::Sql("files.filepath")}};
  ApplyFileCountFilter(filter, {.files = true}, &select);
  for (const db::Row& row : Run(db::Build(select)).rows) {
    result[row.Text(0)] = row.Int(1);
  }
}

void ReportHandler::getFileCountsSummary(
    cc::api::OrderedMap<std::string,
                        cc::api::OrderedMap<std::string, int64_t>>& result,
    const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
    const CompareData& cmp_data, int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select select = {
      .columns = db::Sql(std::format(
          "files.filepath, checkers.severity, reports.review_status, "
          "reports.detection_status, count({}reports.bug_id) AS cnt",
          report_filter.isUnique ? "DISTINCT " : "")),
      .from = db::Sql(
          "files JOIN reports ON reports.file_id = files.id "
          "JOIN checkers ON reports.checker_id = checkers.id"),
      .where = {db::In("files.filepath",
                       DistinctFilePaths(report_filter, filter, limit,
                                         offset))},
      .group_by = {db::Sql(
          "files.filepath, checkers.severity, reports.review_status, "
          "reports.detection_status")}};
  ApplyFileCountFilter(filter, {.checkers = true, .files = true}, &select);
  for (const db::Row& row : Run(db::Build(select)).rows) {
    cc::api::OrderedMap<std::string, int64_t>& summary = result[row.Text(0)];
    const int64_t count = row.Int(4);
    summary["reports"] += count;
    const int severity = static_cast<int>(row.Int(1));
    const auto name = _Severity_VALUES_TO_NAMES.find(severity);
    summary["severity:" + (name != _Severity_VALUES_TO_NAMES.end()
                               ? std::string(name->second)
                               : std::to_string(severity))] += count;
    if (!row.Text(2).empty()) summary["review_status:" + row.Text(2)] += count;
    if (!row.Text(3).empty()) {
      summary["detection_status:" + row.Text(3)] += count;
    }
  }
}

void ReportHandler::getRunHistoryTagCounts(RunTagCounts& result,
                                           const std::vector<int64_t>& run_ids,
                                           const ReportFilter& report_filter,
                                           const CompareData& cmp_data,
                                           int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  limit = VerifyLimitRange(limit);
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(Db(), run_ids, report_filter, &cmp_data));
  db::Select tag_runs = {.columns = db::Sql("run_histories.run_id"),
                         .distinct = true,
                         .from = db::Sql("run_histories")};
  if (!run_ids.empty()) {
    tag_runs.where.push_back(db::In("run_histories.run_id", run_ids));
  }
  db::Select reports = {
      .columns = db::Sql(
          "reports.run_id, reports.bug_id, reports.detected_at, "
          "reports.fixed_at"),
      .from = db::Sql("reports")};
  if (report_filter.__isset.annotations) JoinAnnotations(&reports);
  ApplyReportFilter(filter, {}, &reports);
  reports.where.push_back(db::In("reports.run_id", db::Build(tag_runs)));
  db::Sql counts(std::format(
      "SELECT run_histories.id AS run_history_id, count({}tag_reports.bug_id) "
      "AS report_count FROM run_histories LEFT OUTER JOIN (",
      report_filter.isUnique ? "DISTINCT " : ""));
  counts.Add(db::Build(reports))
      .Add(") AS tag_reports ON tag_reports.run_id = run_histories.run_id "
           "WHERE ")
      .Add(OpenReportsAt("tag_reports", db::Sql("run_histories.time")))
      .Add(" GROUP BY run_histories.id");
  db::Select tags = {.columns = db::Sql(
                         "run_histories.run_id AS run_id, "
                         "run_histories.id AS run_history_id"),
                     .from = db::Sql("run_histories")};
  if (!run_ids.empty()) {
    tags.where.push_back(db::In("run_histories.run_id", run_ids));
  }
  if (report_filter.__isset.runTag) {
    tags.where.push_back(db::In("run_histories.id", report_filter.runTag));
  }
  db::Select select = {
      .columns = db::Sql(
          "tags.run_history_id, max(runs.id), max(runs.name) AS run_name, "
          "max(run_histories.id), max(run_histories.time), "
          "max(run_histories.version_tag), max(tag_counts.report_count)"),
      .from = db::Sql("("),
      .group_by = {db::Sql("tags.run_history_id, run_histories.time")},
      .order_by = {db::Sql("run_histories.time DESC")},
      .limit = limit,
      .offset = offset};
  select.from.Add(db::Build(tags))
      .Add(") AS tags LEFT OUTER JOIN run_histories "
           "ON run_histories.id = tags.run_history_id LEFT OUTER JOIN runs "
           "ON runs.id = tags.run_id LEFT OUTER JOIN (")
      .Add(counts)
      .Add(") AS tag_counts "
           "ON tag_counts.run_history_id = run_histories.id");
  for (const db::Row& row : Run(db::Build(select)).rows) {
    RunTagCount count;
    count.__set_id(row.Int(3));
    count.__set_time(PythonTime(row.OptionalText(4)));
    if (!row.IsNull(5)) count.__set_name(row.Text(5));
    if (!row.IsNull(2)) count.__set_runName(row.Text(2));
    if (!row.IsNull(1)) count.__set_runId(row.Int(1));
    count.__set_count(row.Int(6));
    result.push_back(std::move(count));
  }
}

void ReportHandler::getFailedFiles(FailedFiles& result,
                                   const std::vector<int64_t>& run_ids) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Sql latest(
      "SELECT max(run_histories.id) AS history_id, run_histories.run_id "
      "FROM run_histories");
  if (!run_ids.empty()) {
    latest.Add(" WHERE ").Add(db::In("run_histories.run_id", run_ids));
  }
  latest.Add(" GROUP BY run_histories.run_id");
  db::Sql query(
      "SELECT analyzer_statistics.failed_files, runs.name "
      "FROM analyzer_statistics LEFT OUTER JOIN (");
  query.Add(latest).Add(
      ") AS latest ON analyzer_statistics.run_history_id = latest.history_id "
      "LEFT OUTER JOIN runs ON runs.id = latest.run_id WHERE "
      "analyzer_statistics.run_history_id = latest.history_id AND "
      "analyzer_statistics.failed_files IS NOT NULL");
  for (const db::Row& row : Run(query).rows) {
    const std::string run_name = row.Text(1);
    for (const std::string& file :
         util::Split(util::DecodeUtf8Ignore(Decompress(row.Blob(0))), "\n")) {
      std::vector<AnalysisFailureInfo>& failures = result[file];
      const bool seen = std::ranges::any_of(
          failures, [&run_name](const AnalysisFailureInfo& failure) {
            return failure.runName == run_name;
          });
      if (seen) continue;
      AnalysisFailureInfo failure;
      failure.__set_runName(run_name);
      failures.push_back(std::move(failure));
    }
  }
}

int64_t ReportHandler::getFailedFilesCount(
    const std::vector<int64_t>& run_ids) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  FailedFiles files;
  getFailedFiles(files, run_ids);
  return static_cast<int64_t>(files.size());
}

void ReportHandler::getAnalysisStatistics(AnalyzerStatisticsData& result,
                                          int64_t run_id,
                                          int64_t run_history_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Sql query(
      "SELECT analyzer_statistics.analyzer_type, analyzer_statistics.version, "
      "analyzer_statistics.failed, analyzer_statistics.successful, "
      "analyzer_statistics.failed_files FROM analyzer_statistics "
      "LEFT OUTER JOIN run_histories "
      "ON run_histories.id = analyzer_statistics.run_history_id "
      "LEFT OUTER JOIN runs ON runs.id = run_histories.run_id");
  if (run_id != 0) {
    query
        .Add(
            " WHERE analyzer_statistics.run_history_id IN (SELECT "
            "max(analyzer_statistics.run_history_id) FROM analyzer_statistics "
            "LEFT OUTER JOIN run_histories "
            "ON run_histories.id = analyzer_statistics.run_history_id WHERE "
            "run_histories.run_id = ")
        .Bind(run_id)
        .Add(" GROUP BY run_histories.run_id)");
  } else if (run_history_id != 0) {
    query.Add(" WHERE run_histories.id = ").Bind(run_history_id);
  }
  for (const db::Row& row : Run(query).rows) {
    AnalyzerStatistics statistics;
    if (!row.Blob(1).empty()) {
      statistics.__set_version(Decompress(row.Blob(1)));
    }
    statistics.__set_failed(row.Int(2));
    statistics.__set_successful(row.Int(3));
    statistics.__set_failedFilePaths(
        row.Blob(4).empty() ? std::vector<std::string>()
                            : util::Split(Decompress(row.Blob(4)), "\n"));
    result[row.Text(0)] = std::move(statistics);
  }
}

void ReportHandler::getSourceFileData(SourceFileData& result, int64_t file_id,
                                      bool file_content,
                                      Encoding::type encoding) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::QueryResult files = ValueOrThrow(Db().Query(
      "SELECT id, filepath, content_hash, remote_url, tracking_branch "
      "FROM files WHERE id = ?",
      {file_id}));
  if (files.rows.empty()) return;
  const db::Row& file = files.rows.front();
  const db::QueryResult contents = ValueOrThrow(Db().Query(
      "SELECT content, blame_info FROM file_contents WHERE content_hash = ?",
      {file.Text(2)}));
  if (contents.rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::GENERAL,
        "'NoneType' object has no attribute 'blame_info'");
  }
  const db::Row& content = contents.rows.front();
  result.__set_fileId(file.Int(0));
  result.__set_filePath(file.Text(1));
  result.__set_hasBlameInfo(!content.Blob(1).empty());
  if (std::optional<std::string> url =
          CommitUrl(file.OptionalText(3),
                    request_.server->package_data().git_commit_urls)) {
    result.__set_remoteUrl(*url);
  }
  if (!file.IsNull(4)) result.__set_trackingBranch(file.Text(4));
  if (file_content) {
    std::string source = Decompress(content.Blob(0));
    if (encoding == Encoding::BASE64) source = util::Base64Encode(source);
    result.__set_fileContent(util::DecodeUtf8Ignore(source));
  }
}

void ReportHandler::getBlameInfo(BlameInfo& result, int64_t file_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::QueryResult rows = ValueOrThrow(Db().Query(
      "SELECT file_contents.blame_info FROM files JOIN file_contents "
      "ON file_contents.content_hash = files.content_hash "
      "WHERE files.id = ?",
      {file_id}));
  if (rows.rows.empty() || rows.rows.front().Blob(0).empty()) return;
  // Python reports any error of the decoding with a message that fails to
  // be built, so the error is that of its own failure.
  auto fail = [] {
    return MakeRequestFailed(
        ErrorCode::GENERAL,
        "can only concatenate str (not \"int\") to str");
  };
  const Result<std::string> data =
      util::ZlibDecompress(rows.rows.front().Blob(0));
  if (!data.ok()) throw fail();
  const Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(data.value()));
  if (!document.ok()) throw fail();
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  // Returns the member `key` of `value`; throws if it is missing.
  auto get = [&fail](yyjson_val* value, std::string_view key) {
    Result<yyjson_val*> member = util::JsonSubscript(value, key);
    if (!member.ok()) throw fail();
    return member.value();
  };
  cc::api::OrderedMap<std::string, Commit> commits;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* hash = nullptr;
  yyjson_val* data_value = nullptr;
  yyjson_obj_foreach(get(root, "commits"), index, count, hash, data_value) {
    yyjson_val* author = get(data_value, "author");
    CommitAuthor commit_author;
    commit_author.__set_name(util::JsonString(get(author, "name")));
    commit_author.__set_email(util::JsonString(get(author, "email")));
    Commit commit;
    commit.__set_author(commit_author);
    commit.__set_summary(util::JsonString(get(data_value, "summary")));
    commit.__set_message(
        util::HtmlEscape(util::JsonString(get(data_value, "message"))));
    commit.__set_committedDateTime(
        util::JsonString(get(data_value, "committed_datetime")));
    commits[util::JsonString(hash)] = std::move(commit);
  }
  std::vector<BlameData> blame;
  yyjson_val* range = nullptr;
  yyjson_arr_foreach(get(root, "blame"), index, count, range) {
    BlameData data_range;
    data_range.__set_startLine(
        util::JsonToInt(get(range, "from")).value_or(0));
    data_range.__set_endLine(util::JsonToInt(get(range, "to")).value_or(0));
    data_range.__set_commitHash(util::JsonString(get(range, "commit")));
    blame.push_back(std::move(data_range));
  }
  result.__set_commits(commits);
  result.__set_blame(blame);
}

void ReportHandler::getLinesInSourceFileContents(
    cc::api::OrderedMap<int64_t, cc::api::OrderedMap<int64_t, std::string>>&
        result,
    const LinesInFilesRequestedList& lines_in_files_requested,
    Encoding::type encoding) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  std::map<int64_t, std::vector<std::string>> lines_of_file;
  for (const std::vector<LinesInFilesRequested>& chunk :
       db::Chunks(lines_in_files_requested)) {
    std::vector<int64_t> file_ids;
    for (const LinesInFilesRequested& request : chunk) {
      file_ids.push_back(request.fileId);
    }
    db::Sql query(
        "SELECT file_contents.content, files.id FROM file_contents "
        "JOIN files ON file_contents.content_hash = files.content_hash "
        "WHERE ");
    query.Add(db::In("files.id", file_ids));
    for (const db::Row& row : Run(query).rows) {
      lines_of_file[row.Int(1)] =
          util::Split(util::DecodeUtf8Ignore(Decompress(row.Blob(0))), "\n");
    }
  }
  for (const LinesInFilesRequested& request : lines_in_files_requested) {
    const std::vector<std::string>& lines = lines_of_file[request.fileId];
    const auto size = static_cast<int64_t>(lines.size());
    for (const int64_t line : request.lines) {
      std::string content;
      if (size >= line) {
        // Python's lines[line - 1], which counts from the end below 0.
        int64_t index = line - 1;
        if (index < 0) index += size;
        if (index < 0) {
          throw MakeRequestFailed(ErrorCode::GENERAL,
                                  "list index out of range");
        }
        content = lines[index];
      }
      if (encoding == Encoding::BASE64) {
        content = util::Base64Encode(content);
      }
      result[request.fileId][line] = std::move(content);
    }
  }
}

bool ReportHandler::isReviewStatusChangeDisabled() {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::QueryResult rows = ValueOrThrow(request_.server->config_db().Query(
      "SELECT is_review_status_change_disabled FROM products WHERE id = ?",
      {request_.product->id()}));
  return !rows.rows.empty() && rows.rows.front().Bool(0);
}

bool ReportHandler::changeReviewStatus(int64_t report_id,
                                       ReviewStatus::type status,
                                       const std::string& message) {
  Require({Permission::PRODUCT_ACCESS, Permission::PRODUCT_STORE});
  db::Connection& db = Db();
  const db::QueryResult reports = ValueOrThrow(db.Query(
      "SELECT detection_status, review_status FROM reports WHERE id = ?",
      {report_id}));
  if (reports.rows.empty()) {
    throw MakeRequestFailed(ErrorCode::DATABASE,
                            "No report found in the database.");
  }
  const std::string status_name = ValueOrThrow(ReviewStatusName(status));
  const std::string now = util::FormatDatabaseTime(util::LocalNow());
  Transaction transaction(&db);
  // The reference compares the status value with names, so the fix date is
  // only ever cleared, never set (known-issues #30).
  const db::Row& report = reports.rows.front();
  const std::string detection = report.Text(0);
  const std::string review = report.Text(1);
  if ((detection == "unresolved" || detection == "new" ||
       detection == "reopened") &&
      (review == "false_positive" || review == "intentional")) {
    ValueOrThrow(db.Query("UPDATE reports SET fixed_at = NULL WHERE id = ?",
                          {report_id}));
  }
  ValueOrThrow(db.Query(
      "UPDATE reports SET review_status = ?, review_status_author = ?, "
      "review_status_message = ?, review_status_date = ? WHERE id = ?",
      {status_name, User(), db::Bytes{message}, now, report_id}));
  transaction.Commit();
  kLog.Info(std::format(
      "Review status of report '{}' was changed to '{}' by {}.", report_id,
      status_name, User()));
  return true;
}

db::Select ReportHandler::ReviewStatusRuleQuery(
    const ReviewStatusRuleFilter& filter,
    const ReviewStatusRuleSortMode* sort_mode) {
  db::Select select = {
      .columns = db::Sql(
          "review_statuses.bug_hash, review_statuses.status, "
          "review_statuses.author, review_statuses.message, "
          "review_statuses.date, count(reports.id) AS report_count"),
      .from = db::Sql(
          "review_statuses LEFT OUTER JOIN reports "
          "ON reports.bug_id = review_statuses.bug_hash"),
      .group_by = {db::Sql("review_statuses.bug_hash")}};
  // Returns the OR of the ILIKE of `column` and each of `patterns`.
  auto any_like = [](std::string_view column,
                     const std::vector<std::string>& patterns) {
    std::vector<db::Sql> any;
    for (const std::string& pattern : patterns) {
      any.push_back(db::ILike(db::Sql(column), Conv(pattern)));
    }
    return db::Or(any);
  };
  if (!filter.reportHashes.empty()) {
    select.where.push_back(
        any_like("review_statuses.bug_hash", filter.reportHashes));
  }
  if (filter.__isset.reviewStatuses) {
    std::vector<std::string> names;
    for (const ReviewStatus::type status : filter.reviewStatuses) {
      names.push_back(ValueOrThrow(ReviewStatusName(status)));
    }
    select.where.push_back(db::In("review_statuses.status", names));
  }
  if (!filter.authors.empty()) {
    select.where.push_back(any_like("review_statuses.author", filter.authors));
  }
  if (filter.noAssociatedReports) {
    select.having.emplace_back("count(reports.id) = 0");
  }
  if (sort_mode != nullptr) {
    std::string_view column;
    switch (sort_mode->type) {
      case ReviewStatusRuleSortType::REPORT_HASH:
        column = "review_statuses.bug_hash";
        break;
      case ReviewStatusRuleSortType::STATUS:
        column = "review_statuses.status";
        break;
      case ReviewStatusRuleSortType::AUTHOR:
        column = "review_statuses.author";
        break;
      case ReviewStatusRuleSortType::DATE:
        column = "review_statuses.date";
        break;
      case ReviewStatusRuleSortType::ASSOCIATED_REPORTS_COUNT:
        column = "report_count";
        break;
    }
    if (!column.empty()) {
      select.order_by.emplace_back(std::format(
          "{} {}", column, sort_mode->ord == Order::DESC ? "DESC" : "ASC"));
    }
  }
  return select;
}

void ReportHandler::getReviewStatusRules(
    ReviewStatusRules& result, const ReviewStatusRuleFilter& filter,
    const ReviewStatusRuleSortMode& sort_mode, int64_t limit, int64_t offset) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  ReviewStatusRuleSortMode order = sort_mode;
  if (!sort_mode.__isset.type && !sort_mode.__isset.ord) {
    order.__set_type(ReviewStatusRuleSortType::DATE);
    order.__set_ord(Order::DESC);
  }
  // Returns the rules of `chunk`, a filter of a part of the report hashes:
  // long lists made the ILIKE matching too slow.
  auto add_rules = [&](const ReviewStatusRuleFilter& chunk) {
    db::Select select = ReviewStatusRuleQuery(chunk, &order);
    if (limit != 0) {
      select.limit = limit;
      select.offset = offset;
    }
    for (const db::Row& row : Run(db::Build(select)).rows) {
      ReviewStatusRule rule;
      rule.__set_reportHash(row.Text(0));
      rule.__set_reviewData(MakeReviewData(
          row.OptionalText(1),
          row.IsNull(3) ? std::nullopt
                        : std::optional<std::string>(row.Blob(3)),
          row.OptionalText(2), row.OptionalText(4), /*is_in_source=*/false));
      rule.__set_associatedReportCount(row.Int(5));
      result.push_back(std::move(rule));
    }
  };
  if (filter.reportHashes.empty()) {
    add_rules(filter);
    return;
  }
  for (const std::vector<std::string>& hashes :
       db::Chunks(filter.reportHashes)) {
    ReviewStatusRuleFilter chunk = filter;
    chunk.__set_reportHashes(hashes);
    add_rules(chunk);
  }
}

int64_t ReportHandler::getReviewStatusRulesCount(
    const ReviewStatusRuleFilter& filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::Select select = ReviewStatusRuleQuery(filter, nullptr);
  return Run(db::Count(db::Build(select))).rows.front().Int(0);
}

namespace {

// Returns the list `values` as Python's repr() writes it; "None" if
// `is_set` is false.
template <typename T>
std::string ListRepr(const std::vector<T>& values, bool is_set) {
  if (!is_set) return "None";
  std::vector<std::string> items;
  for (const T& value : values) {
    if constexpr (std::is_same_v<T, std::string>) {
      items.push_back(util::Repr(value));
    } else {
      items.push_back(std::to_string(static_cast<int64_t>(value)));
    }
  }
  return "[" + util::Join(items, ", ") + "]";
}

// Returns `filter` as Python's repr() of the Thrift object writes it.
std::string FilterRepr(const ReviewStatusRuleFilter& filter) {
  const auto& set = filter.__isset;
  if (!set.reportHashes && !set.reviewStatuses && !set.authors &&
      !set.noAssociatedReports) {
    return "None";
  }
  return std::format(
      "ReviewStatusRuleFilter(reportHashes={}, reviewStatuses={}, "
      "authors={}, noAssociatedReports={})",
      ListRepr(filter.reportHashes, set.reportHashes),
      ListRepr(filter.reviewStatuses, set.reviewStatuses),
      ListRepr(filter.authors, set.authors),
      set.noAssociatedReports ? (filter.noAssociatedReports ? "True" : "False")
                              : "None");
}

// Returns the bytes `data` as Python's repr() of bytes writes them.
std::string BytesRepr(std::string_view data) {
  const bool double_quotes = data.find('\'') != std::string_view::npos &&
                             data.find('"') == std::string_view::npos;
  const char quote = double_quotes ? '"' : '\'';
  std::string repr = std::format("b{}", quote);
  for (const char c : data) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == quote || c == '\\') {
      repr += '\\';
      repr += c;
    } else if (c == '\t') {
      repr += "\\t";
    } else if (c == '\n') {
      repr += "\\n";
    } else if (c == '\r') {
      repr += "\\r";
    } else if (byte < 0x20 || byte >= 0x7F) {
      repr += std::format("\\x{:02x}", byte);
    } else {
      repr += c;
    }
  }
  return repr + quote;
}

}  // namespace

bool ReportHandler::removeReviewStatusRules(
    const ReviewStatusRuleFilter& filter) {
  Require({Permission::PRODUCT_ADMIN});
  db::Connection& db = Db();
  Transaction transaction(&db);
  for (const db::Row& row :
       Run(db::Build(ReviewStatusRuleQuery(filter, nullptr))).rows) {
    const std::string hash = row.Text(0);
    ValueOrThrow(
        db.Query("DELETE FROM review_statuses WHERE bug_hash = ?", {hash}));
    // Reports become unreviewed unless a source code comment reviews them.
    ValueOrThrow(db.Query(
        "UPDATE reports SET review_status = 'unreviewed', "
        "review_status_author = NULL, review_status_message = NULL, "
        "review_status_date = NULL, fixed_at = NULL WHERE bug_id = ? AND "
        "review_status_is_in_source IS false",
        {hash}));
  }
  transaction.Commit();
  kLog.Info(std::format(
      "Review status rules were removed based on filter '{}' by'{}'.",
      FilterRepr(filter), User()));
  return true;
}

bool ReportHandler::addReviewStatusRule(const std::string& report_hash,
                                        ReviewStatus::type status,
                                        const std::string& message) {
  Require({Permission::PRODUCT_ACCESS, Permission::PRODUCT_STORE});
  if (isReviewStatusChangeDisabled()) {
    throw MakeRequestFailed(ErrorCode::GENERAL,
                            "Review status change is disabled!");
  }
  Transaction transaction(&Db());
  SetReviewStatus(report_hash, status, message, std::nullopt);
  transaction.Commit();
  return true;
}

void ReportHandler::getComments(CommentDataList& result, int64_t report_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::QueryResult reports = ValueOrThrow(
      Db().Query("SELECT bug_id FROM reports WHERE id = ?", {report_id}));
  if (reports.rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Report id {} was not found in the database.", report_id));
  }
  const db::QueryResult comments = ValueOrThrow(Db().Query(
      std::format("SELECT {} FROM comments WHERE comments.bug_hash = ? "
                  "ORDER BY comments.created_at DESC",
                  kCommentColumns),
      {reports.rows.front().Text(0)}));
  for (const db::Row& row : comments.rows) {
    const int64_t kind = row.Int(5);
    if (kind != kUserComment && kind != kSystemComment) {
      throw MakeRequestFailed(
          ErrorCode::GENERAL,
          std::format("Unknown CommentKindValue: {}", kind));
    }
    result.push_back(ToCommentData(row, CommentMessage(kind, row.Blob(3))));
  }
}

int64_t ReportHandler::getCommentCount(int64_t report_id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const db::QueryResult reports = ValueOrThrow(
      Db().Query("SELECT bug_id FROM reports WHERE id = ?", {report_id}));
  if (reports.rows.empty()) return 0;
  return ValueOrThrow(
             Db().Query("SELECT count(*) FROM comments WHERE bug_hash = ?",
                        {reports.rows.front().Text(0)}))
      .rows.front()
      .Int(0);
}

bool ReportHandler::addComment(int64_t report_id, const CommentData& comment) {
  Require({Permission::PRODUCT_ACCESS});
  if (util::Strip(comment.message).empty()) {
    throw MakeRequestFailed(ErrorCode::GENERAL,
                            "The comment message can not be empty!");
  }
  const db::QueryResult reports = ValueOrThrow(
      Db().Query("SELECT bug_id FROM reports WHERE id = ?", {report_id}));
  if (reports.rows.empty()) {
    const std::string message =
        std::format("Report id {} was not found in the database.", report_id);
    kLog.Error(message);
    throw MakeRequestFailed(ErrorCode::DATABASE, message);
  }
  AddComment(reports.rows.front().Text(0), comment.message, kUserComment,
             util::LocalNow());
  return true;
}

bool ReportHandler::updateComment(int64_t comment_id,
                                  const std::string& new_message) {
  Require({Permission::PRODUCT_ACCESS});
  if (util::Strip(new_message).empty()) {
    throw MakeRequestFailed(ErrorCode::GENERAL,
                            "The comment message can not be empty!");
  }
  db::Connection& db = Db();
  const db::QueryResult comments = ValueOrThrow(db.Query(
      std::format("SELECT {} FROM comments WHERE comments.id = ?",
                  kCommentColumns),
      {comment_id}));
  if (comments.rows.empty()) {
    const std::string message = std::format(
        "Comment id {} was not found in the database.", comment_id);
    kLog.Error(message);
    throw MakeRequestFailed(ErrorCode::DATABASE, message);
  }
  const db::Row& comment = comments.rows.front();
  const std::string author = comment.Text(2);
  if (author != "Anonymous" && author != User()) {
    throw MakeRequestFailed(ErrorCode::UNAUTHORIZED,
                            "Unathorized comment modification!");
  }
  Transaction transaction(&db);
  const std::string old_message = comment.Blob(3);
  if (old_message != new_message) {
    AddComment(comment.Text(1),
               std::format("comment_changed {} {}",
                           util::ShlexQuote(old_message),
                           util::ShlexQuote(new_message)),
               kSystemComment, util::LocalNow());
  }
  ValueOrThrow(db.Query("UPDATE comments SET message = ? WHERE id = ?",
                        {db::Bytes{new_message}, comment_id}));
  transaction.Commit();
  return true;
}

bool ReportHandler::removeComment(int64_t comment_id) {
  Require({Permission::PRODUCT_ACCESS});
  db::Connection& db = Db();
  const db::QueryResult comments = ValueOrThrow(db.Query(
      std::format("SELECT {} FROM comments WHERE comments.id = ?",
                  kCommentColumns),
      {comment_id}));
  if (comments.rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Comment id {} was not found in the database.",
                    comment_id));
  }
  const db::Row& comment = comments.rows.front();
  const std::string author = comment.Text(2);
  if (author != "Anonymous" && author != User()) {
    throw MakeRequestFailed(ErrorCode::UNAUTHORIZED,
                            "Unathorized comment modification!");
  }
  ValueOrThrow(db.Query("DELETE FROM comments WHERE id = ?", {comment_id}));
  const std::string message = comment.Blob(3);
  kLog.Info(std::format(
      "Comment '{}...' was removed from bug hash '{}' by '{}'.",
      BytesRepr(std::string_view(message).substr(0, 10)), comment.Text(1),
      User()));
  return true;
}

void ReportHandler::getCheckerDoc(std::string& result,
                                  const std::string& /*checker_id*/) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  result.clear();
}

void ReportHandler::getCheckerLabels(
    std::vector<std::vector<std::string>>& result,
    const std::vector<::Checker>& checkers) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const common::CheckerLabels& labels =
      request_.server->package_data().checker_labels;
  for (const ::Checker& checker : checkers) {
    std::optional<std::string_view> analyzer;
    if (!checker.analyzerName.empty() &&
        util::AsciiLower(checker.analyzerName) != "unknown") {
      analyzer = checker.analyzerName;
    }
    std::vector<std::string> names;
    for (const auto& [key, value] :
         labels.AllLabelsOfChecker(checker.checkerId, analyzer)) {
      names.push_back(std::format("{}:{}", key, value));
    }
    result.push_back(std::move(names));
  }
}

void ReportHandler::getGuidelineRules(
    GuidelineRules& result, const std::vector<Guideline>& guidelines) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  const PackageData& data = request_.server->package_data();
  for (const Guideline& guideline : guidelines) {
    const std::string& name = guideline.guidelineName;
    std::vector<Rule>& rules = result[name];
    for (const common::Guidelines::Rule& rule :
         data.guidelines.RulesOfGuideline(name)) {
      Rule api_rule;
      api_rule.__set_ruleId(util::AsciiLower(rule.id));
      api_rule.__set_title(rule.title);
      api_rule.__set_url(rule.url);
      api_rule.__set_checkers(data.checker_labels.CheckersByLabels(
          {std::format("{}:{}", name, rule.id)}));
      api_rule.__set_level(rule.level);
      rules.push_back(std::move(api_rule));
    }
  }
}

void ReportHandler::getPackageVersion(std::string& result) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  result = request_.server->package_version();
}

Result<bool> ReportHandler::RemoveRuns(const std::vector<int64_t>& run_ids) {
  db::Connection& db = Db();
  db::Sql query("SELECT runs.id, runs.name FROM runs WHERE ");
  query.Add(db::In("runs.id", run_ids));
  Result<db::QueryResult> runs = db.Query(query.text(), query.params());
  if (!runs.ok()) return Result<bool>(runs.error());
  std::vector<int64_t> matched;
  for (const db::Row& row : runs.value().rows) matched.push_back(row.Int(0));
  if (!matched.empty()) {
    if (Result<void> unlocked = CheckRemoveRunsLock(matched);
        !unlocked.ok()) {
      return Result<bool>(unlocked.error());
    }
  }
  // Each run is removed in a transaction of its own: removing a run is slow
  // because of the cascades, and a statement timeout of the database must
  // not be reached.
  std::vector<std::string> names;
  int64_t deleted = 0;
  for (const db::Row& row : runs.value().rows) {
    names.push_back(row.Text(1));
    const Result<void> done = db::RunInTransaction(db, [&db, &row] {
      Result<db::QueryResult> removed =
          db.Query("DELETE FROM runs WHERE id = ?", {row.Int(0)});
      return removed.ok() ? Result<void>() : Result<void>(removed.error());
    });
    if (done.ok()) {
      ++deleted;
    } else {
      kLog.Warning(std::format(
          "Suppressed an exception while deleting run {}. Error: {}",
          row.Text(1), done.error().message));
    }
  }
  kLog.Info(std::format("Runs '{}' were removed by '{}'.",
                        util::Join(names, "', '"), User()));
  if (Result<void> counted = ChangeCachedRunCount(-deleted); !counted.ok()) {
    return Result<bool>(counted.error());
  }
  // Unused files are kept: a store running at the same time may use them.
  db::RemoveUnusedComments(db, request_.product->endpoint());
  db::RemoveUnusedAnalysisInfo(db, request_.product->endpoint());
  return Result<bool>(!names.empty());
}

bool ReportHandler::removeRunResults(const std::vector<int64_t>& run_ids) {
  Require({Permission::PRODUCT_STORE});
  bool failed = false;
  for (const int64_t run_id : run_ids) {
    // Python: removeRun(run_id, None), which checks the permission again.
    Require({Permission::PRODUCT_STORE});
    const Result<bool> removed = RemoveRuns({run_id});
    if (!removed.ok()) {
      kLog.Error(std::format("Failed to remove run: {}", run_id));
      kLog.Error(RequestFailedRepr(ErrorCode::DATABASE,
                                   removed.error().message));
      failed = true;
    }
  }
  return !failed;
}

bool ReportHandler::RemoveReports(const std::vector<int64_t>& run_ids,
                                  const ReportFilter& report_filter,
                                  const CompareData& cmp_data) {
  db::Connection& db = Db();
  const ReportFilterSql filter = ValueOrThrow(
      ProcessReportFilter(db, run_ids, report_filter, &cmp_data));
  db::Select select = {.columns = db::Sql("reports.id"),
                       .from = db::Sql("reports")};
  if (report_filter.__isset.annotations) JoinAnnotations(&select);
  ApplyReportFilter(filter, {}, &select);
  std::vector<int64_t> report_ids;
  const Result<void> done = db::RunInTransaction(db, [&] {
    const db::Sql query = db::Build(select);
    Result<db::QueryResult> rows = db.Query(query.text(), query.params());
    if (!rows.ok()) return Result<void>(rows.error());
    for (const db::Row& row : rows.value().rows) {
      report_ids.push_back(row.Int(0));
    }
    for (const std::vector<int64_t>& chunk : db::Chunks(report_ids)) {
      db::Sql remove("DELETE FROM reports WHERE ");
      remove.Add(db::In("id", chunk));
      Result<db::QueryResult> removed =
          db.Query(remove.text(), remove.params());
      if (!removed.ok()) return Result<void>(removed.error());
    }
    return Result<void>();
  });
  if (!done.ok()) {
    kLog.Error("Database cleanup failed.");
    kLog.Error(done.error().message);
    return false;
  }
  kLog.Info(std::format("The following reports were removed by '{}': {}",
                        User(), ListRepr(report_ids, /*is_set=*/true)));
  return true;
}

bool ReportHandler::removeRunReports(const std::vector<int64_t>& run_ids,
                                     const ReportFilter& report_filter,
                                     const CompareData& cmp_data) {
  Require({Permission::PRODUCT_STORE});
  std::vector<int64_t> all_run_ids = run_ids;
  all_run_ids.insert(all_run_ids.end(), cmp_data.runIds.begin(),
                     cmp_data.runIds.end());
  ThrowIfFailed(CheckRemoveRunsLock(all_run_ids));
  if (!RemoveReports(all_run_ids, report_filter, cmp_data)) return false;
  // Unused files are kept: a store running at the same time may use them.
  db::RemoveUnusedComments(Db(), request_.product->endpoint());
  db::RemoveUnusedAnalysisInfo(Db(), request_.product->endpoint());
  return true;
}

bool ReportHandler::removeRun(int64_t run_id, const RunFilter& run_filter) {
  Require({Permission::PRODUCT_STORE});
  RunFilter filter = run_filter;
  if (IsUnset(run_filter)) filter.__set_ids({run_id});
  // The runs are chosen before the lock check, and exactly those are
  // removed: the filter may match a newly stored, locked run later.
  db::Select select = {.columns = db::Sql("runs.id"),
                       .from = db::Sql("runs")};
  ThrowIfFailed(ProcessRunFilter(Db(), filter, &select));
  std::vector<int64_t> matched;
  for (const db::Row& row : Run(db::Build(select)).rows) {
    matched.push_back(row.Int(0));
  }
  return ValueOrThrow(RemoveRuns(matched));
}

bool ReportHandler::updateRunData(int64_t run_id,
                                  const std::string& new_run_name) {
  Require({Permission::PRODUCT_STORE});
  // Logs `message` and returns it as a RequestFailed with `code`.
  auto fail = [](ErrorCode::type code, const std::string& message) {
    kLog.Error(message);
    return MakeRequestFailed(code, message);
  };
  if (new_run_name.empty()) {
    throw fail(ErrorCode::GENERAL,
               "No new run name was given to update the run.");
  }
  db::Connection& db = Db();
  if (!ValueOrThrow(db.Query("SELECT id FROM runs WHERE name = ?",
                             {new_run_name}))
           .rows.empty()) {
    throw fail(ErrorCode::DATABASE,
               std::format("New run name '{}' already exists.", new_run_name));
  }
  const db::QueryResult runs =
      ValueOrThrow(db.Query("SELECT name FROM runs WHERE id = ?", {run_id}));
  if (runs.rows.empty()) {
    throw fail(ErrorCode::DATABASE,
               std::format("Run id {} was not found in the database.",
                           run_id));
  }
  ValueOrThrow(db.Query("UPDATE runs SET name = ? WHERE id = ?",
                        {new_run_name, run_id}));
  kLog.Info(std::format("Run name '{}' ({}) was changed to {} by '{}'.",
                        runs.rows.front().Text(0), run_id, new_run_name,
                        User()));
  return true;
}

void ReportHandler::getSuppressFile(std::string& result) {
  Require({Permission::PRODUCT_ACCESS});
  result.clear();
}

namespace {

// Returns `text` as a parameter; NULL if it is empty, which is how a None
// of the client arrives.
db::Param TextOrNull(const std::string& text) {
  return text.empty() ? db::Param(nullptr) : db::Param(text);
}

// Returns the query of the ids of the files whose path matches any of the
// LIKE patterns `paths` (Python: get_include_skip_queries()).
db::Sql FilesLike(const std::vector<std::string>& paths) {
  std::vector<db::Sql> any;
  for (const std::string& path : paths) {
    db::Sql like("files.filepath LIKE ");
    any.push_back(like.Bind(Conv(path)));
  }
  db::Sql query("SELECT DISTINCT files.id FROM files WHERE ");
  return query.Add(db::Or(any));
}

}  // namespace

bool ReportHandler::addSourceComponent(const std::string& name,
                                       const std::string& value,
                                       const std::string& description) {
  Require({Permission::PRODUCT_ADMIN});
  db::Connection& db = Db();
  Transaction transaction(&db);
  const bool exists =
      !ValueOrThrow(db.Query(
                        "SELECT name FROM source_components WHERE name = ?",
                        {name}))
           .rows.empty();
  if (exists) {
    // As in the reference, the user name is not updated (known-issues #38).
    ValueOrThrow(db.Query(
        "UPDATE source_components SET value = ?, description = ? "
        "WHERE name = ?",
        {db::Bytes{value}, TextOrNull(description), name}));
  } else {
    ValueOrThrow(db.Query(
        "INSERT INTO source_components (name, value, description, username) "
        "VALUES (?, ?, ?, ?)",
        {name, db::Bytes{value}, TextOrNull(description),
         request_.session ? db::Param(request_.session->user)
                          : db::Param(nullptr)}));
  }
  // Python: update_source_component_files().
  ValueOrThrow(db.Query(
      "DELETE FROM source_component_files WHERE source_component_name = ?",
      {name}));
  const auto [include, skip] = ParseComponentValues(value);
  db::Sql files;
  if (!include.empty() && !skip.empty()) {
    files = FilesLike(include);
    files.Add(" EXCEPT ").Add(FilesLike(skip));
  } else if (!include.empty()) {
    files = FilesLike(include);
  } else if (!skip.empty()) {
    files = db::Sql("SELECT files.id FROM files WHERE files.id NOT IN (");
    files.Add(FilesLike(skip)).Add(")");
  }
  if (!files.empty()) {
    for (const db::Row& row : Run(files).rows) {
      ValueOrThrow(db.Query(
          "INSERT INTO source_component_files (source_component_name, "
          "file_id) VALUES (?, ?)",
          {name, row.Int(0)}));
    }
  }
  transaction.Commit();
  return true;
}

void ReportHandler::getSourceComponents(
    SourceComponentDataList& result,
    const std::vector<std::string>& component_filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Select select = {
      .columns = db::Sql("source_components.name, source_components.value, "
                         "source_components.description"),
      .from = db::Sql("source_components"),
      .order_by = {db::Sql("source_components.name")}};
  // As in the reference, a component must match every filter.
  for (const std::string& name : component_filter) {
    select.where.push_back(
        db::ILike(db::Sql("source_components.name"), Conv(name)));
  }
  for (const db::Row& row : Run(db::Build(select)).rows) {
    SourceComponentData component;
    component.__set_name(row.Text(0));
    component.__set_value(row.Blob(1));
    if (!row.IsNull(2)) component.__set_description(row.Text(2));
    result.push_back(std::move(component));
  }
  if (component_filter.empty() ||
      std::ranges::find(component_filter, kOtherComponentName) !=
          component_filter.end()) {
    SourceComponentData other;
    other.__set_name(std::string(kOtherComponentName));
    other.__set_description(
        "Special auto-generated source component which contains files that "
        "are uncovered by the rest of the components.");
    result.push_back(std::move(other));
  }
}

bool ReportHandler::removeSourceComponent(const std::string& name) {
  Require({Permission::PRODUCT_ADMIN});
  const db::QueryResult removed = ValueOrThrow(
      Db().Query("DELETE FROM source_components WHERE name = ?", {name}));
  if (removed.changed_rows == 0) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Source component {} was not found in the database.",
                    name));
  }
  kLog.Info(std::format("Source component '{}' has been removed by '{}'",
                        name, User()));
  return true;
}

std::vector<std::string> ReportHandler::MissingContentHashes(
    const std::vector<std::string>& hashes, bool for_blame) {
  std::set<std::string> found;
  for (const std::vector<std::string>& chunk : db::Chunks(hashes)) {
    db::Sql query("SELECT content_hash FROM file_contents WHERE ");
    query.Add(db::In("content_hash", chunk));
    if (for_blame) query.Add(" AND blame_info IS NOT NULL");
    for (const db::Row& row : Run(query).rows) found.insert(row.Text(0));
  }
  // Python returns a set difference; each hash is listed once.
  std::vector<std::string> missing;
  std::set<std::string> listed;
  for (const std::string& hash : hashes) {
    if (!found.contains(hash) && listed.insert(hash).second) {
      missing.push_back(hash);
    }
  }
  return missing;
}

void ReportHandler::getMissingContentHashes(
    std::vector<std::string>& result,
    const std::vector<std::string>& file_hashes) {
  Require({Permission::PRODUCT_STORE});
  result = MissingContentHashes(file_hashes, /*for_blame=*/false);
}

void ReportHandler::getMissingContentHashesForBlameInfo(
    std::vector<std::string>& result,
    const std::vector<std::string>& file_hashes) {
  Require({Permission::PRODUCT_STORE});
  result = MissingContentHashes(file_hashes, /*for_blame=*/true);
}

namespace {

// Returns `text` as an optional value; nullopt if it is empty, which is how
// a None of the client arrives.
std::optional<std::string> TextOrNone(const std::string& text) {
  return text.empty() ? std::nullopt : std::optional<std::string>(text);
}

// Returns `text` with the characters other than letters, digits, '_',
// whitespace, '-' and '/' removed, and with runs of whitespace and of '/'
// replaced by '_' (Python: slugify()).
std::string Slugify(const std::string& text) {
  static const auto& kSpecial = *new util::Regex(R"([^\w\s\-/])");
  static const auto& kSeparators = *new util::Regex(R"(([\s]+|[/]+))");
  return kSeparators.ReplaceAll(kSpecial.ReplaceAll(text, ""), "_");
}

}  // namespace

int64_t ReportHandler::massStoreRun(
    const std::string& run_name, const std::string& tag,
    const std::string& version, const std::string& zipfile, bool force,
    const std::vector<std::string>& trim_path_prefixes,
    const std::string& description) {
  Require({Permission::PRODUCT_STORE});
  const std::string token = ValueOrThrow(SubmitStore(
      request_,
      {.run_name = run_name,
       .tag = TextOrNone(tag),
       .client_version = version,
       .force = force,
       .trim_path_prefixes = trim_path_prefixes,
       .description = TextOrNone(description),
       .user_name = User()},
      zipfile, /*asynchronous=*/false));
  // Clients older than 6.25 wait for this call, which used to store the
  // results itself.
  const tasks::TaskRecord task = ValueOrThrow(
      request_.server->tasks().GetTaskRecord(token), ErrorCode::GENERAL);
  if (task.status == "failed") {
    throw MakeRequestFailed(
        ErrorCode::GENERAL,
        std::format("massStoreRun()'s processing failed. Here follow the "
                    "details:\n\n{}",
                    task.comments.value_or("None")));
  }
  if (task.status == "cancelled") {
    throw MakeRequestFailed(ErrorCode::GENERAL,
                            "Server administrators cancelled the processing "
                            "of the massStoreRun() request!");
  }
  return -1;
}

void ReportHandler::massStoreRunAsynchronous(
    ::TaskToken& result, const std::string& zipfile_blob,
    const SubmittedRunOptions& store_opts) {
  Require({Permission::PRODUCT_STORE});
  auto optional = [](bool is_set, const std::string& text) {
    return is_set ? std::optional<std::string>(text) : std::nullopt;
  };
  std::optional<std::vector<std::string>> trim_path_prefixes;
  if (store_opts.__isset.trimPathPrefixes) {
    trim_path_prefixes = store_opts.trimPathPrefixes;
  }
  result = ValueOrThrow(SubmitStore(
      request_,
      {.run_name = store_opts.runName,
       .tag = optional(store_opts.__isset.tag, store_opts.tag),
       .client_version = store_opts.version,
       .force = store_opts.force,
       .trim_path_prefixes = std::move(trim_path_prefixes),
       .description =
           optional(store_opts.__isset.description, store_opts.description),
       .user_name = User()},
      zipfile_blob, /*asynchronous=*/true));
}

bool ReportHandler::allowsStoringAnalysisStatistics() {
  Require({Permission::PRODUCT_STORE});
  return !request_.server->config().AnalysisStatisticsDir().value_or("")
              .empty();
}

void ReportHandler::getAnalysisStatisticsLimits(
    cc::api::OrderedMap<StoreLimitKind::type, int64_t>& result) {
  Require({Permission::PRODUCT_STORE});
  const ServerConfig& config = request_.server->config();
  if (const int64_t size = config.FailureZipSizeLimit().value_or(0)) {
    result[StoreLimitKind::FAILURE_ZIP_SIZE] = size;
  }
  if (const int64_t size =
          config.CompilationDatabaseSizeLimit().value_or(0)) {
    result[StoreLimitKind::COMPILATION_DATABASE_SIZE] = size;
  }
}

bool ReportHandler::storeAnalysisStatistics(const std::string& run_name,
                                            const std::string& zipfile) {
  Require({Permission::PRODUCT_STORE});
  const std::string directory =
      request_.server->config().AnalysisStatisticsDir().value_or("");
  if (directory.empty()) return false;
  const std::string product_dir =
      util::pypath::Join({directory, request_.product->endpoint()});
  const std::string zip_path = util::pypath::Join(
      {product_dir, Slugify(run_name) + ".zip"});
  // Only the user and the group may read the statistics.
  constexpr int kDirectoryMode = 0740;
  constexpr int kFileMode = 0640;
  if (!util::pypath::Exists(product_dir)) {
    if (const std::error_code error = platform::MakeDirs(product_dir)) {
      kLog.Error(util::OsError(error.value(), product_dir).message);
      return false;
    }
    (void)platform::SetFileMode(product_dir, kDirectoryMode);
  }
  const std::optional<std::string> compressed = util::Base64Decode(zipfile);
  if (!compressed) {
    kLog.Error("Incorrect padding");
    return false;
  }
  const Result<std::string> zip = util::ZlibDecompress(*compressed);
  const Result<void> written =
      zip.ok() ? util::WriteFile(zip_path, zip.value())
               : Result<void>(zip.error());
  if (!written.ok()) {
    kLog.Error(written.error().message);
    return false;
  }
  (void)platform::SetFileMode(zip_path, kFileMode);
  return true;
}

void ReportHandler::exportData(ExportData& result,
                               const RunFilter& run_filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Connection& db = Db();
  const bool filtered = !IsUnset(run_filter);
  db::Select comments = {
      .columns = db::Sql(std::format("{}, reports.bug_id", kCommentColumns)),
      .from = db::Sql(
          "comments LEFT OUTER JOIN reports "
          "ON reports.bug_id = comments.bug_hash"),
      .order_by = {db::Sql("comments.created_at DESC, comments.id DESC")}};
  db::Select reviews = {
      .columns = db::Sql(
          "reports.bug_id, reports.review_status, "
          "reports.review_status_message, reports.review_status_author, "
          "reports.review_status_date, reports.review_status_is_in_source"),
      .from = db::Sql("reports"),
      .where = {db::Sql("reports.review_status != 'unreviewed'")},
      .order_by = {db::Sql("reports.review_status_date")}};
  if (filtered) {
    for (db::Select* select : {&comments, &reviews}) {
      ThrowIfFailed(ProcessRunFilter(db, run_filter, select));
      select->from.Add(" LEFT OUTER JOIN runs ON reports.run_id = runs.id");
    }
  }
  cc::api::OrderedMap<std::string, std::vector<CommentData>> comment_data;
  // The join repeats a comment for each report with its hash; SQLAlchemy
  // keeps the first of the same rows.
  std::set<std::pair<int64_t, std::string>> seen;
  for (const db::Row& row : Run(db::Build(comments)).rows) {
    // Comments of no report are removed by the clean-ups.
    if (row.IsNull(6)) continue;
    if (!seen.emplace(row.Int(0), row.Text(6)).second) continue;
    comment_data[row.Text(6)].push_back(
        ToCommentData(row, util::HtmlUnescape(row.Blob(3))));
  }
  cc::api::OrderedMap<std::string, ReviewData> review_data;
  for (const db::Row& row : Run(db::Build(reviews)).rows) {
    review_data[row.Text(0)] = MakeReviewData(
        row.OptionalText(1),
        row.IsNull(2) ? std::nullopt : std::optional<std::string>(row.Blob(2)),
        row.OptionalText(3), row.OptionalText(4), row.Bool(5));
  }
  result.__set_comments(comment_data);
  result.__set_reviewData(review_data);
}

bool ReportHandler::importData(const ExportData& export_data) {
  Require({Permission::PRODUCT_ADMIN});
  db::Connection& db = Db();
  Transaction transaction(&db);
  std::vector<std::string> comment_hashes;
  for (const auto& [hash, comments] : export_data.comments) {
    comment_hashes.push_back(hash);
  }
  db::Sql comment_query(std::format("SELECT {} FROM comments WHERE ",
                                    kCommentColumns));
  comment_query.Add(db::In("comments.bug_hash", comment_hashes))
      .Add(" ORDER BY comments.created_at DESC");
  const db::QueryResult existing = Run(comment_query);
  // Every date is checked before the first insert, as the reference adds
  // the comments to its session and inserts them later.
  std::vector<int64_t> dates;
  for (const auto& [hash, comments] : export_data.comments) {
    for (const CommentData& comment : comments) {
      dates.push_back(ValueOrThrow(ParseTime(comment.createdAt)));
    }
  }
  size_t next_date = 0;
  for (const auto& [hash, comments] : export_data.comments) {
    for (const CommentData& comment : comments) {
      const int64_t date = dates[next_date++];
      // The comment may be in the database already.
      const bool stored = std::ranges::any_of(
          existing.rows, [&](const db::Row& row) {
            return row.Text(1) == hash &&
                   util::ParseDatabaseTime(row.Text(4)) == date &&
                   row.Int(5) == static_cast<int64_t>(comment.kind) &&
                   row.Blob(3) == comment.message;
          });
      if (stored) continue;
      ValueOrThrow(db.Query(
          "INSERT INTO comments (bug_hash, author, message, kind, "
          "created_at) VALUES (?, ?, ?, ?, ?)",
          {hash, comment.author, db::Bytes{comment.message},
           static_cast<int64_t>(comment.kind),
           util::FormatDatabaseTime(date)}));
    }
  }
  std::vector<std::string> review_hashes;
  for (const auto& [hash, review] : export_data.reviewData) {
    review_hashes.push_back(hash);
  }
  db::Sql review_query(
      "SELECT review_statuses.bug_hash, review_statuses.date "
      "FROM review_statuses WHERE ");
  review_query.Add(db::In("review_statuses.bug_hash", review_hashes))
      .Add(" ORDER BY review_statuses.date DESC");
  std::map<std::string, std::string> review_dates;
  for (const db::Row& row : Run(review_query).rows) {
    review_dates[row.Text(0)] = util::DatabaseTimeToPython(row.Text(1));
  }
  for (const auto& [hash, review] : export_data.reviewData) {
    // The status is up to date.
    if (const auto stored = review_dates.find(hash);
        stored != review_dates.end() && stored->second == review.date) {
      continue;
    }
    SetReviewStatus(hash, review.status, review.comment,
                    ValueOrThrow(ParseTime(review.date)));
  }
  transaction.Commit();
  return true;
}

int64_t ReportHandler::addCleanupPlan(const std::string& name,
                                      const std::string& description,
                                      int64_t due_date) {
  Require({Permission::PRODUCT_ADMIN});
  db::Connection& db = Db();
  if (!ValueOrThrow(db.Query("SELECT id FROM cleanup_plans WHERE name = ?",
                             {name}))
           .rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("Cleanup plan '{}' already exists.", name));
  }
  ValueOrThrow(db.Query(
      "INSERT INTO cleanup_plans (name, description, due_date) "
      "VALUES (?, ?, ?)",
      {name, TextOrNull(description),
       due_date != 0 ? db::Param(TimeParam(due_date)) : db::Param(nullptr)}));
  const int64_t id =
      ValueOrThrow(db.Query("SELECT id FROM cleanup_plans WHERE name = ?",
                            {name}))
          .rows.front()
          .Int(0);
  kLog.Info(std::format("New cleanup plan '{}' has been created by '{}'",
                        name, User()));
  return id;
}

bool ReportHandler::updateCleanupPlan(int64_t id, const std::string& name,
                                      const std::string& description,
                                      int64_t due_date) {
  Require({Permission::PRODUCT_ADMIN});
  CleanupPlanRow(id);
  ValueOrThrow(Db().Query(
      "UPDATE cleanup_plans SET name = ?, description = ?, due_date = ? "
      "WHERE id = ?",
      {name, TextOrNull(description),
       due_date != 0 ? db::Param(TimeParam(due_date)) : db::Param(nullptr),
       id}));
  kLog.Info(std::format("Cleanup plan '{}' has been updated by '{}'", id,
                        User()));
  return true;
}

void ReportHandler::getCleanupPlans(CleanupPlans& result,
                                    const CleanupPlanFilter& filter) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  db::Select select = {.columns = db::Sql(
                           "cleanup_plans.id, cleanup_plans.name, "
                           "cleanup_plans.due_date, cleanup_plans.description, "
                           "cleanup_plans.closed_at"),
                       .from = db::Sql("cleanup_plans"),
                       .order_by = {db::Sql("cleanup_plans.name")}};
  if (!filter.ids.empty()) {
    select.where.push_back(db::In("cleanup_plans.id", filter.ids));
  }
  if (!filter.names.empty()) {
    select.where.push_back(db::In("cleanup_plans.name", filter.names));
  }
  if (filter.__isset.isOpen) {
    select.where.emplace_back(filter.isOpen
                                  ? "cleanup_plans.closed_at IS NULL"
                                  : "cleanup_plans.closed_at IS NOT NULL");
  }
  const db::QueryResult plans = Run(db::Build(select));
  std::vector<int64_t> ids;
  for (const db::Row& row : plans.rows) ids.push_back(row.Int(0));
  std::map<int64_t, std::vector<std::string>> hashes;
  db::Sql hash_query(
      "SELECT cleanup_plan_id, bug_hash FROM cleanup_plan_report_hashes "
      "WHERE ");
  hash_query.Add(db::In("cleanup_plan_id", ids));
  for (const db::Row& row : Run(hash_query).rows) {
    hashes[row.Int(0)].push_back(row.Text(1));
  }
  // Returns the Unix time of the database time in `column` of `row`.
  auto timestamp = [](const db::Row& row, int column) {
    const std::optional<int64_t> seconds = util::ToTimestamp(
        util::ParseDatabaseTime(row.Text(column)).value_or(0));
    if (!seconds) {
      throw MakeRequestFailed(ErrorCode::GENERAL,
                              "mktime argument out of range");
    }
    return *seconds;
  };
  for (const db::Row& row : plans.rows) {
    ::CleanupPlan plan;
    plan.__set_id(row.Int(0));
    plan.__set_name(row.Text(1));
    if (!row.IsNull(3)) plan.__set_description(row.Text(3));
    if (!row.IsNull(2)) plan.__set_dueDate(timestamp(row, 2));
    if (!row.IsNull(4)) plan.__set_closedAt(timestamp(row, 4));
    plan.__set_reportHashes(hashes[row.Int(0)]);
    result.push_back(std::move(plan));
  }
}

bool ReportHandler::removeCleanupPlan(int64_t cleanup_plan_id) {
  Require({Permission::PRODUCT_ADMIN});
  const db::Row plan = CleanupPlanRow(cleanup_plan_id);
  ValueOrThrow(Db().Query("DELETE FROM cleanup_plans WHERE id = ?",
                          {cleanup_plan_id}));
  kLog.Info(std::format("Cleanup plan '{}' has been removed by '{}'",
                        plan.Text(1), User()));
  return true;
}

bool ReportHandler::closeCleanupPlan(int64_t cleanup_plan_id) {
  Require({Permission::PRODUCT_ADMIN});
  const db::Row plan = CleanupPlanRow(cleanup_plan_id);
  ValueOrThrow(Db().Query(
      "UPDATE cleanup_plans SET closed_at = ? WHERE id = ?",
      {util::FormatDatabaseTime(util::LocalNow()), cleanup_plan_id}));
  kLog.Info(std::format("Cleanup plan '{}' has been closed by '{}'",
                        plan.Text(1), User()));
  return true;
}

bool ReportHandler::reopenCleanupPlan(int64_t cleanup_plan_id) {
  Require({Permission::PRODUCT_ADMIN});
  const db::Row plan = CleanupPlanRow(cleanup_plan_id);
  ValueOrThrow(Db().Query(
      "UPDATE cleanup_plans SET closed_at = NULL WHERE id = ?",
      {cleanup_plan_id}));
  kLog.Info(std::format("Cleanup plan '{}' has been reopened by '{}'",
                        plan.Text(1), User()));
  return true;
}

bool ReportHandler::setCleanupPlan(
    int64_t cleanup_plan_id, const std::vector<std::string>& report_hashes) {
  Require({Permission::PRODUCT_ADMIN});
  CleanupPlanRow(cleanup_plan_id);
  db::Connection& db = Db();
  db::Sql query(
      "SELECT bug_hash FROM cleanup_plan_report_hashes WHERE "
      "cleanup_plan_id = ");
  query.Bind(cleanup_plan_id)
      .Add(" AND ")
      .Add(db::In("bug_hash", report_hashes));
  std::set<std::string> hashes(report_hashes.begin(), report_hashes.end());
  for (const db::Row& row : Run(query).rows) hashes.erase(row.Text(0));
  Transaction transaction(&db);
  for (const std::string& hash : hashes) {
    ValueOrThrow(db.Query(
        "INSERT INTO cleanup_plan_report_hashes (cleanup_plan_id, bug_hash) "
        "VALUES (?, ?)",
        {cleanup_plan_id, hash}));
  }
  transaction.Commit();
  return true;
}

bool ReportHandler::unsetCleanupPlan(
    int64_t cleanup_plan_id, const std::vector<std::string>& report_hashes) {
  Require({Permission::PRODUCT_ADMIN});
  CleanupPlanRow(cleanup_plan_id);
  db::Sql remove(
      "DELETE FROM cleanup_plan_report_hashes WHERE cleanup_plan_id = ");
  remove.Bind(cleanup_plan_id)
      .Add(" AND ")
      .Add(db::In("bug_hash", report_hashes));
  Run(remove);
  return true;
}

namespace {

// Returns the JSON text of `filter` that a filter preset stores: the fields
// that are set, in the order of the API (Python:
// json.dumps(thrift_to_json(filter))).
std::string ReportFilterJson(const ReportFilter& filter) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  const auto& set = filter.__isset;
  auto add = [doc, root](const char* key, yyjson_mut_val* value) {
    yyjson_mut_obj_add_val(doc, root, key, value);
  };
  auto strings = [doc](const std::vector<std::string>& values) {
    yyjson_mut_val* array = yyjson_mut_arr(doc);
    for (const std::string& value : values) {
      yyjson_mut_arr_append(array, util::MakeJsonString(doc, value));
    }
    return array;
  };
  auto integers = [doc]<typename T>(const std::vector<T>& values) {
    yyjson_mut_val* array = yyjson_mut_arr(doc);
    for (const T value : values) {
      yyjson_mut_arr_add_sint(doc, array, static_cast<int64_t>(value));
    }
    return array;
  };
  auto interval = [doc](const DateInterval& dates) {
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    if (dates.__isset.before) {
      yyjson_mut_obj_add_sint(doc, object, "before", dates.before);
    }
    if (dates.__isset.after) {
      yyjson_mut_obj_add_sint(doc, object, "after", dates.after);
    }
    return object;
  };
  if (set.filepath) add("filepath", strings(filter.filepath));
  if (set.checkerMsg) add("checkerMsg", strings(filter.checkerMsg));
  if (set.checkerName) add("checkerName", strings(filter.checkerName));
  if (set.reportHash) add("reportHash", strings(filter.reportHash));
  if (set.severity) add("severity", integers(filter.severity));
  if (set.reviewStatus) add("reviewStatus", integers(filter.reviewStatus));
  if (set.detectionStatus) {
    add("detectionStatus", integers(filter.detectionStatus));
  }
  if (set.runHistoryTag) add("runHistoryTag", strings(filter.runHistoryTag));
  if (set.firstDetectionDate) {
    add("firstDetectionDate",
        yyjson_mut_sint(doc, filter.firstDetectionDate));
  }
  if (set.fixDate) add("fixDate", yyjson_mut_sint(doc, filter.fixDate));
  if (set.isUnique) add("isUnique", yyjson_mut_bool(doc, filter.isUnique));
  if (set.runName) add("runName", strings(filter.runName));
  if (set.runTag) add("runTag", integers(filter.runTag));
  if (set.componentNames) {
    add("componentNames", strings(filter.componentNames));
  }
  if (set.bugPathLength) {
    yyjson_mut_val* range = yyjson_mut_obj(doc);
    if (filter.bugPathLength.__isset.min) {
      yyjson_mut_obj_add_sint(doc, range, "min", filter.bugPathLength.min);
    }
    if (filter.bugPathLength.__isset.max) {
      yyjson_mut_obj_add_sint(doc, range, "max", filter.bugPathLength.max);
    }
    add("bugPathLength", range);
  }
  if (set.date) {
    yyjson_mut_val* date = yyjson_mut_obj(doc);
    if (filter.date.__isset.detected) {
      yyjson_mut_obj_add_val(doc, date, "detected",
                             interval(filter.date.detected));
    }
    if (filter.date.__isset.fixed) {
      yyjson_mut_obj_add_val(doc, date, "fixed", interval(filter.date.fixed));
    }
    add("date", date);
  }
  if (set.analyzerNames) add("analyzerNames", strings(filter.analyzerNames));
  if (set.openReportsDate) {
    add("openReportsDate", yyjson_mut_sint(doc, filter.openReportsDate));
  }
  if (set.cleanupPlanNames) {
    add("cleanupPlanNames", strings(filter.cleanupPlanNames));
  }
  if (set.fileMatchesAnyPoint) {
    add("fileMatchesAnyPoint",
        yyjson_mut_bool(doc, filter.fileMatchesAnyPoint));
  }
  if (set.componentMatchesAnyPoint) {
    add("componentMatchesAnyPoint",
        yyjson_mut_bool(doc, filter.componentMatchesAnyPoint));
  }
  if (set.annotations) {
    yyjson_mut_val* pairs = yyjson_mut_arr(doc);
    for (const Pair& annotation : filter.annotations) {
      yyjson_mut_val* pair = yyjson_mut_obj(doc);
      if (annotation.__isset.first) {
        yyjson_mut_obj_add_val(doc, pair, "first",
                               util::MakeJsonString(doc, annotation.first));
      }
      if (annotation.__isset.second) {
        yyjson_mut_obj_add_val(doc, pair, "second",
                               util::MakeJsonString(doc, annotation.second));
      }
      yyjson_mut_arr_append(pairs, pair);
    }
    add("annotations", pairs);
  }
  if (set.reportStatus) add("reportStatus", integers(filter.reportStatus));
  if (set.fullReportPathInComponent) {
    add("fullReportPathInComponent",
        yyjson_mut_bool(doc, filter.fullReportPathInComponent));
  }
  return util::DumpJson(root);
}

// Returns the integers of the JSON array `array` as values of `T`, an
// integer or enum type; 0 for an item that is not an integer.
template <typename T>
std::vector<T> JsonIntegers(yyjson_val* array) {
  std::vector<T> values;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* value = nullptr;
  yyjson_arr_foreach(array, index, count, value) {
    values.push_back(static_cast<T>(util::JsonToInt(value).value_or(0)));
  }
  return values;
}

// Returns the report filter that `stored`, the report_filter column of a
// filter preset, holds (Python: transform_rf_db_to_thrift()). The column
// is a JSON string with the JSON text of the filter, as SQLAlchemy writes
// the text into a JSON column.
ReportFilter ReportFilterOfPreset(const std::string& stored) {
  const util::JsonDocument column =
      ValueOrThrow(util::ParseJson(stored), ErrorCode::GENERAL);
  const util::JsonDocument document = ValueOrThrow(
      util::ParseJson(util::JsonString(yyjson_doc_get_root(column.get()))),
      ErrorCode::GENERAL);
  yyjson_val* root = yyjson_doc_get_root(document.get());
  // Returns the member `key` of `value` unless it is missing or null
  // (Python: rf_db.get(key) is not None).
  auto get = [](yyjson_val* value, std::string_view key) -> yyjson_val* {
    yyjson_val* member = util::JsonGet(value, key);
    return yyjson_is_null(member) ? nullptr : member;
  };
  auto strings = [](yyjson_val* array) {
    std::vector<std::string> values;
    size_t index = 0;
    size_t count = 0;
    yyjson_val* value = nullptr;
    yyjson_arr_foreach(array, index, count, value) {
      values.push_back(util::JsonString(value));
    }
    return values;
  };
  auto number = [](yyjson_val* value) {
    return util::JsonToInt(value).value_or(0);
  };
  auto interval = [&get, &number](yyjson_val* object) {
    DateInterval dates;
    if (yyjson_val* before = get(object, "before")) {
      dates.__set_before(number(before));
    }
    if (yyjson_val* after = get(object, "after")) {
      dates.__set_after(number(after));
    }
    return dates;
  };
  ReportFilter filter;
  if (yyjson_val* v = get(root, "filepath")) filter.__set_filepath(strings(v));
  if (yyjson_val* v = get(root, "checkerMsg")) {
    filter.__set_checkerMsg(strings(v));
  }
  if (yyjson_val* v = get(root, "checkerName")) {
    filter.__set_checkerName(strings(v));
  }
  if (yyjson_val* v = get(root, "reportHash")) {
    filter.__set_reportHash(strings(v));
  }
  if (yyjson_val* v = get(root, "severity")) {
    filter.__set_severity(JsonIntegers<Severity::type>(v));
  }
  if (yyjson_val* v = get(root, "reviewStatus")) {
    filter.__set_reviewStatus(JsonIntegers<ReviewStatus::type>(v));
  }
  if (yyjson_val* v = get(root, "detectionStatus")) {
    filter.__set_detectionStatus(JsonIntegers<DetectionStatus::type>(v));
  }
  if (yyjson_val* v = get(root, "runHistoryTag")) {
    filter.__set_runHistoryTag(strings(v));
  }
  if (yyjson_val* v = get(root, "firstDetectionDate")) {
    filter.__set_firstDetectionDate(number(v));
  }
  if (yyjson_val* v = get(root, "fixDate")) filter.__set_fixDate(number(v));
  if (yyjson_val* v = get(root, "isUnique")) {
    filter.__set_isUnique(util::JsonTruthy(v));
  }
  if (yyjson_val* v = get(root, "runName")) filter.__set_runName(strings(v));
  if (yyjson_val* v = get(root, "runTag")) {
    filter.__set_runTag(JsonIntegers<int64_t>(v));
  }
  if (yyjson_val* v = get(root, "componentNames")) {
    filter.__set_componentNames(strings(v));
  }
  if (yyjson_val* v = get(root, "bugPathLength"); util::JsonTruthy(v)) {
    BugPathLengthRange range;
    if (yyjson_val* min = get(v, "min")) range.__set_min(number(min));
    if (yyjson_val* max = get(v, "max")) range.__set_max(number(max));
    filter.__set_bugPathLength(range);
  }
  if (yyjson_val* v = get(root, "date"); util::JsonTruthy(v)) {
    ReportDate date;
    if (yyjson_val* detected = get(v, "detected");
        util::JsonTruthy(detected)) {
      date.__set_detected(interval(detected));
    }
    if (yyjson_val* fixed = get(v, "fixed"); util::JsonTruthy(fixed)) {
      date.__set_fixed(interval(fixed));
    }
    filter.__set_date(date);
  }
  if (yyjson_val* v = get(root, "analyzerNames")) {
    filter.__set_analyzerNames(strings(v));
  }
  if (yyjson_val* v = get(root, "openReportsDate")) {
    filter.__set_openReportsDate(number(v));
  }
  if (yyjson_val* v = get(root, "cleanupPlanNames")) {
    filter.__set_cleanupPlanNames(strings(v));
  }
  if (yyjson_val* v = get(root, "fileMatchesAnyPoint")) {
    filter.__set_fileMatchesAnyPoint(util::JsonTruthy(v));
  }
  if (yyjson_val* v = get(root, "componentMatchesAnyPoint")) {
    filter.__set_componentMatchesAnyPoint(util::JsonTruthy(v));
  }
  // As in the reference, the annotations are always set.
  std::vector<Pair> annotations;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* annotation = nullptr;
  yyjson_arr_foreach(get(root, "annotations"), index, count, annotation) {
    Pair pair;
    if (yyjson_val* first = get(annotation, "first")) {
      pair.__set_first(util::JsonString(first));
    }
    if (yyjson_val* second = get(annotation, "second")) {
      pair.__set_second(util::JsonString(second));
    }
    annotations.push_back(std::move(pair));
  }
  filter.__set_annotations(annotations);
  if (yyjson_val* v = get(root, "reportStatus")) {
    filter.__set_reportStatus(JsonIntegers<ReportStatus::type>(v));
  }
  if (yyjson_val* v = get(root, "fullReportPathInComponent")) {
    filter.__set_fullReportPathInComponent(util::JsonTruthy(v));
  }
  return filter;
}

// Returns the filter preset in the columns "id, preset_name, report_filter"
// of `row`.
FilterPreset ToFilterPreset(const db::Row& row) {
  FilterPreset preset;
  preset.__set_id(row.Int(0));
  preset.__set_name(row.Text(1));
  preset.__set_reportFilter(ReportFilterOfPreset(row.Text(2)));
  return preset;
}

// Returns the error of a filter preset call that failed because of
// `message`, as the reference wraps its own RequestFailed in another one.
::RequestFailed PresetFailure(std::string_view prefix,
                              const std::string& message) {
  return MakeRequestFailed(
      ErrorCode::DATABASE,
      std::format("{}{}", prefix,
                  RequestFailedRepr(ErrorCode::DATABASE, message)));
}

}  // namespace

int64_t ReportHandler::storeFilterPreset(const FilterPreset& preset) {
  Require({Permission::PRODUCT_ADMIN});
  kLog.Info(std::format("Storing filter preset in backend: {}", preset.name));
  constexpr std::string_view kFailure =
      "CodeChecker could not store the filter preset: ";
  const std::string name(util::Strip(preset.name));
  // SQLAlchemy writes the JSON text into the JSON column as a JSON string.
  const util::MutableJsonDocument column = util::NewJsonDocument();
  const std::string report_filter = util::DumpJson(util::MakeJsonString(
      column.get(), ReportFilterJson(preset.reportFilter)));
  db::Connection& db = Db();
  const bool name_exists =
      !ValueOrThrow(db.Query("SELECT id FROM filter_presets "
                             "WHERE preset_name = ?",
                             {name}))
           .rows.empty();
  if (preset.id == -1) {
    if (name_exists) {
      throw PresetFailure(
          kFailure,
          std::format("A filter preset with name '{}' already exists!", name));
    }
    ValueOrThrow(db.Query(
        "INSERT INTO filter_presets (preset_name, report_filter) "
        "VALUES (?, ?)",
        {name, report_filter}));
    return ValueOrThrow(db.Query("SELECT id FROM filter_presets "
                                 "WHERE preset_name = ?",
                                 {name}))
        .rows.front()
        .Int(0);
  }
  const db::QueryResult updated = ValueOrThrow(db.Query(
      "UPDATE filter_presets SET preset_name = ?, report_filter = ? "
      "WHERE id = ?",
      {name, report_filter, preset.id}));
  if (updated.changed_rows == 0) {
    throw PresetFailure(
        kFailure,
        std::format("No filter preset found with id {}!", preset.id));
  }
  return preset.id;
}

int64_t ReportHandler::renameFilterPreset(int64_t id,
                                          const std::string& name) {
  Require({Permission::PRODUCT_ADMIN});
  constexpr std::string_view kFailure =
      "CodeChecker could not rename filter preset: ";
  const std::string new_name(util::Strip(name));
  if (new_name.empty()) {
    throw PresetFailure(kFailure, "Preset name cannot be empty!");
  }
  if (id == 0) throw PresetFailure(kFailure, "Preset ID cannot be empty!");
  db::Connection& db = Db();
  const db::QueryResult presets = ValueOrThrow(db.Query(
      "SELECT id, preset_name FROM filter_presets WHERE id = ?", {id}));
  if (presets.rows.empty()) {
    throw PresetFailure(kFailure,
                        std::format("No filter preset found with id {}!", id));
  }
  const db::QueryResult same_name = ValueOrThrow(db.Query(
      "SELECT id FROM filter_presets WHERE preset_name = ?", {new_name}));
  if (!same_name.rows.empty() && same_name.rows.front().Int(0) != id) {
    throw PresetFailure(
        kFailure,
        std::format("A filter preset with name '{}' already exists!",
                    new_name));
  }
  kLog.Info(std::format(
      "Renaming filter preset {{ id: {}, name: {} }} with name: '{}' ", id,
      presets.rows.front().Text(1), new_name));
  ValueOrThrow(db.Query(
      "UPDATE filter_presets SET preset_name = ? WHERE id = ?",
      {new_name, id}));
  return id;
}

int64_t ReportHandler::deleteFilterPreset(int64_t id) {
  Require({Permission::PRODUCT_ADMIN});
  kLog.Info(std::format("Deleting filter preset by ID: {}", id));
  const db::QueryResult removed = ValueOrThrow(
      Db().Query("DELETE FROM filter_presets WHERE id = ?", {id}));
  if (removed.changed_rows == 0) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("No filter preset found with id {}!", id));
  }
  return id;
}

void ReportHandler::getFilterPreset(FilterPreset& result, int64_t id) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  kLog.Info(std::format("Returning filter preset by ID: {}", id));
  const db::QueryResult presets = ValueOrThrow(Db().Query(
      "SELECT id, preset_name, report_filter FROM filter_presets "
      "WHERE id = ?",
      {id}));
  if (presets.rows.empty()) {
    throw MakeRequestFailed(
        ErrorCode::DATABASE,
        std::format("No filter preset found with id {}!", id));
  }
  result = ToFilterPreset(presets.rows.front());
}

void ReportHandler::listFilterPreset(std::vector<FilterPreset>& result) {
  Require({Permission::PRODUCT_VIEW, Permission::PERMISSION_VIEW});
  kLog.Info("List back filter presets");
  for (const db::Row& row :
       Run(db::Sql("SELECT id, preset_name, report_filter "
                   "FROM filter_presets"))
           .rows) {
    result.push_back(ToFilterPreset(row));
  }
}

}  // namespace cc::server::api
