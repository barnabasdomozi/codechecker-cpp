#include "client/cmd/cmd_line.h"

#include <stdint.h>
#include <yyjson.h>

#include <string>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/report_server_types.h"
#include "util/json.h"

namespace cc::client::cmd {
namespace {

// The values of the fields. The overloads for structs are declared first,
// because the list and map templates below call them for their elements.
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, int64_t value) {
  return yyjson_mut_sint(doc, value);
}
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const std::string& value) {
  return util::MakeJsonString(doc, value);
}
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const AnalyzerStatistics& stats);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BugPathEvent& event);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BugPathPos& position);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const ExtendedReportData& data);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const CommentData& comment);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BlameData& blame);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const Commit& commit);

// Returns the text json writes for a map key: a string as it is, a number
// as its decimal text.
std::string JsonKey(const std::string& key) { return key; }
std::string JsonKey(DetectionStatus::type key) {
  return std::to_string(int64_t{key});
}

template <typename T>
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const std::vector<T>& values) {
  yyjson_mut_val* array = yyjson_mut_arr(doc);
  for (const T& value : values) {
    yyjson_mut_arr_append(array, ToJson(doc, value));
  }
  return array;
}

template <typename Key, typename Value>
yyjson_mut_val* ToJson(yyjson_mut_doc* doc,
                       const api::OrderedMap<Key, Value>& map) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  for (const auto& [key, value] : map) {
    yyjson_mut_obj_add(object, util::MakeJsonString(doc, JsonKey(key)),
                       ToJson(doc, value));
  }
  return object;
}

// Adds the field `key` to `object`: `value` if the field is set, else null
// (Python: None). `key` must be a string literal; yyjson keeps the pointer.
void AddField(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* key,
              bool is_set, yyjson_mut_val* value) {
  yyjson_mut_obj_add_val(doc, object, key,
                         is_set ? value : yyjson_mut_null(doc));
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const AnalyzerStatistics& stats) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = stats.__isset;
  AddField(doc, object, "version", set.version, ToJson(doc, stats.version));
  AddField(doc, object, "failed", set.failed, ToJson(doc, stats.failed));
  AddField(doc, object, "successful", set.successful,
           ToJson(doc, stats.successful));
  AddField(doc, object, "failedFilePaths", set.failedFilePaths,
           ToJson(doc, stats.failedFilePaths));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BugPathEvent& event) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = event.__isset;
  AddField(doc, object, "startLine", set.startLine,
           ToJson(doc, event.startLine));
  AddField(doc, object, "startCol", set.startCol, ToJson(doc, event.startCol));
  AddField(doc, object, "endLine", set.endLine, ToJson(doc, event.endLine));
  AddField(doc, object, "endCol", set.endCol, ToJson(doc, event.endCol));
  AddField(doc, object, "msg", set.msg, ToJson(doc, event.msg));
  AddField(doc, object, "fileId", set.fileId, ToJson(doc, event.fileId));
  AddField(doc, object, "filePath", set.filePath, ToJson(doc, event.filePath));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BugPathPos& position) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = position.__isset;
  AddField(doc, object, "startLine", set.startLine,
           ToJson(doc, position.startLine));
  AddField(doc, object, "startCol", set.startCol,
           ToJson(doc, position.startCol));
  AddField(doc, object, "endLine", set.endLine, ToJson(doc, position.endLine));
  AddField(doc, object, "endCol", set.endCol, ToJson(doc, position.endCol));
  AddField(doc, object, "fileId", set.fileId, ToJson(doc, position.fileId));
  AddField(doc, object, "filePath", set.filePath,
           ToJson(doc, position.filePath));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const ExtendedReportData& data) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = data.__isset;
  AddField(doc, object, "type", set.type,
           ToJson(doc, int64_t{data.type}));
  AddField(doc, object, "startLine", set.startLine,
           ToJson(doc, data.startLine));
  AddField(doc, object, "startCol", set.startCol, ToJson(doc, data.startCol));
  AddField(doc, object, "endLine", set.endLine, ToJson(doc, data.endLine));
  AddField(doc, object, "endCol", set.endCol, ToJson(doc, data.endCol));
  AddField(doc, object, "message", set.message, ToJson(doc, data.message));
  AddField(doc, object, "fileId", set.fileId, ToJson(doc, data.fileId));
  AddField(doc, object, "filePath", set.filePath, ToJson(doc, data.filePath));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const CommentData& comment) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = comment.__isset;
  AddField(doc, object, "id", set.id, ToJson(doc, comment.id));
  AddField(doc, object, "author", set.author, ToJson(doc, comment.author));
  AddField(doc, object, "message", set.message, ToJson(doc, comment.message));
  AddField(doc, object, "createdAt", set.createdAt,
           ToJson(doc, comment.createdAt));
  AddField(doc, object, "kind", set.kind,
           ToJson(doc, int64_t{comment.kind}));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BlameData& blame) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = blame.__isset;
  AddField(doc, object, "startLine", set.startLine,
           ToJson(doc, blame.startLine));
  AddField(doc, object, "endLine", set.endLine, ToJson(doc, blame.endLine));
  AddField(doc, object, "commitHash", set.commitHash,
           ToJson(doc, blame.commitHash));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const CommitAuthor& author) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = author.__isset;
  AddField(doc, object, "name", set.name, ToJson(doc, author.name));
  AddField(doc, object, "email", set.email, ToJson(doc, author.email));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const Commit& commit) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = commit.__isset;
  AddField(doc, object, "author", set.author, ToJson(doc, commit.author));
  AddField(doc, object, "summary", set.summary, ToJson(doc, commit.summary));
  AddField(doc, object, "message", set.message, ToJson(doc, commit.message));
  AddField(doc, object, "committedDateTime", set.committedDateTime,
           ToJson(doc, commit.committedDateTime));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const BlameInfo& blame_info) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = blame_info.__isset;
  AddField(doc, object, "commits", set.commits,
           ToJson(doc, blame_info.commits));
  AddField(doc, object, "blame", set.blame, ToJson(doc, blame_info.blame));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const ReportDetails& details) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = details.__isset;
  AddField(doc, object, "pathEvents", set.pathEvents,
           ToJson(doc, details.pathEvents));
  AddField(doc, object, "executionPath", set.executionPath,
           ToJson(doc, details.executionPath));
  AddField(doc, object, "extendedData", set.extendedData,
           ToJson(doc, details.extendedData));
  AddField(doc, object, "comments", set.comments,
           ToJson(doc, details.comments));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const ReviewData& review) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = review.__isset;
  AddField(doc, object, "status", set.status,
           ToJson(doc, int64_t{review.status}));
  AddField(doc, object, "comment", set.comment, ToJson(doc, review.comment));
  AddField(doc, object, "author", set.author, ToJson(doc, review.author));
  AddField(doc, object, "date", set.date, ToJson(doc, review.date));
  AddField(doc, object, "isInSource", set.isInSource,
           yyjson_mut_bool(doc, review.isInSource));
  return object;
}

}  // namespace

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const RunData& run) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = run.__isset;
  AddField(doc, object, "runId", set.runId, ToJson(doc, run.runId));
  AddField(doc, object, "runDate", set.runDate, ToJson(doc, run.runDate));
  AddField(doc, object, "name", set.name, ToJson(doc, run.name));
  AddField(doc, object, "duration", set.duration, ToJson(doc, run.duration));
  AddField(doc, object, "resultCount", set.resultCount,
           ToJson(doc, run.resultCount));
  AddField(doc, object, "runCmd", set.runCmd, ToJson(doc, run.runCmd));
  AddField(doc, object, "detectionStatusCount", set.detectionStatusCount,
           ToJson(doc, run.detectionStatusCount));
  AddField(doc, object, "versionTag", set.versionTag,
           ToJson(doc, run.versionTag));
  AddField(doc, object, "codeCheckerVersion", set.codeCheckerVersion,
           ToJson(doc, run.codeCheckerVersion));
  AddField(doc, object, "analyzerStatistics", set.analyzerStatistics,
           ToJson(doc, run.analyzerStatistics));
  AddField(doc, object, "description", set.description,
           ToJson(doc, run.description));
  return object;
}

yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const ReportData& report) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto& set = report.__isset;
  AddField(doc, object, "runId", set.runId, ToJson(doc, report.runId));
  AddField(doc, object, "checkerId", set.checkerId,
           ToJson(doc, report.checkerId));
  AddField(doc, object, "bugHash", set.bugHash, ToJson(doc, report.bugHash));
  AddField(doc, object, "checkedFile", set.checkedFile,
           ToJson(doc, report.checkedFile));
  AddField(doc, object, "checkerMsg", set.checkerMsg,
           ToJson(doc, report.checkerMsg));
  AddField(doc, object, "reportId", set.reportId, ToJson(doc, report.reportId));
  AddField(doc, object, "fileId", set.fileId, ToJson(doc, report.fileId));
  AddField(doc, object, "line", set.line, ToJson(doc, report.line));
  AddField(doc, object, "column", set.column, ToJson(doc, report.column));
  AddField(doc, object, "severity", set.severity,
           ToJson(doc, int64_t{report.severity}));
  AddField(doc, object, "reviewData", set.reviewData,
           ToJson(doc, report.reviewData));
  AddField(doc, object, "detectionStatus", set.detectionStatus,
           ToJson(doc, int64_t{report.detectionStatus}));
  AddField(doc, object, "detectedAt", set.detectedAt,
           ToJson(doc, report.detectedAt));
  AddField(doc, object, "fixedAt", set.fixedAt, ToJson(doc, report.fixedAt));
  AddField(doc, object, "bugPathLength", set.bugPathLength,
           ToJson(doc, report.bugPathLength));
  AddField(doc, object, "details", set.details, ToJson(doc, report.details));
  AddField(doc, object, "analyzerName", set.analyzerName,
           ToJson(doc, report.analyzerName));
  AddField(doc, object, "annotations", set.annotations,
           ToJson(doc, report.annotations));
  AddField(doc, object, "blameInfo", set.blameInfo,
           ToJson(doc, report.blameInfo));
  return object;
}

}  // namespace cc::client::cmd
