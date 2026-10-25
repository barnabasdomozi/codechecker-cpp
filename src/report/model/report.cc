#include "report/model/report.h"

#include <yyjson.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/file.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

yyjson_mut_val* String(yyjson_mut_doc* doc, std::string_view text) {
  return yyjson_mut_strncpy(doc, text.data(), text.size());
}

yyjson_mut_val* OptionalString(yyjson_mut_doc* doc,
                               const std::optional<std::string>& text) {
  return text ? String(doc, *text) : yyjson_mut_null(doc);
}

yyjson_mut_val* FileToJson(const File& file, yyjson_mut_doc* doc) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, object, "id", String(doc, file.id));
  yyjson_mut_obj_add_val(doc, object, "path", String(doc, file.path));
  yyjson_mut_obj_add_val(doc, object, "original_path",
                         String(doc, file.original_path));
  return object;
}

yyjson_mut_val* RangeToJson(const std::optional<Range>& range,
                            yyjson_mut_doc* doc) {
  if (!range) return yyjson_mut_null(doc);
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, object, "start_line", range->start_line);
  yyjson_mut_obj_add_int(doc, object, "start_col", range->start_col);
  yyjson_mut_obj_add_int(doc, object, "end_line", range->end_line);
  yyjson_mut_obj_add_int(doc, object, "end_col", range->end_col);
  return object;
}

yyjson_mut_val* EventToJson(const BugPathEvent& event, yyjson_mut_doc* doc,
                            const std::string* macro_name = nullptr) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  if (macro_name != nullptr) {
    yyjson_mut_obj_add_val(doc, object, "name", String(doc, *macro_name));
  }
  yyjson_mut_obj_add_val(doc, object, "file", FileToJson(event.file, doc));
  yyjson_mut_obj_add_int(doc, object, "line", event.line);
  yyjson_mut_obj_add_int(doc, object, "column", event.column);
  yyjson_mut_obj_add_val(doc, object, "message", String(doc, event.message));
  yyjson_mut_obj_add_val(doc, object, "range", RangeToJson(event.range, doc));
  return object;
}

}  // namespace

File MakeFile(const std::string& path) {
  return File{.id = path, .path = path, .original_path = path};
}

bool operator==(const File& a, const File& b) { return a.id == b.id; }

Report MakeReport(File file, int line, int column, std::string message,
                  std::string checker_name,
                  std::optional<std::vector<BugPathEvent>> events) {
  Report report{.file = std::move(file),
                .line = line,
                .column = column,
                .message = std::move(message),
                .checker_name = std::move(checker_name)};
  report.static_message = report.message;
  if (events) {
    report.bug_path_events = std::move(*events);
  } else {
    report.bug_path_events.push_back({.message = report.message,
                                      .file = report.file,
                                      .line = line,
                                      .column = column});
  }
  return report;
}

std::set<std::string> OriginalFiles(const Report& report) {
  std::set<std::string> files = {report.file.original_path};
  for (const BugPathEvent& event : report.bug_path_events) {
    files.insert(event.file.original_path);
  }
  for (const BugPathPosition& position : report.bug_path_positions) {
    files.insert(position.file.original_path);
  }
  for (const BugPathEvent& note : report.notes) {
    files.insert(note.file.original_path);
  }
  for (const MacroExpansion& macro : report.macro_expansions) {
    files.insert(macro.event.file.original_path);
  }
  return files;
}

yyjson_mut_val* ReportToJson(const Report& report, yyjson_mut_doc* doc) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto add = [doc, object](const char* key, yyjson_mut_val* value) {
    yyjson_mut_obj_add_val(doc, object, key, value);
  };
  add("analyzer_result_file_path",
      OptionalString(doc, report.analyzer_result_file_path));
  add("file", FileToJson(report.file, doc));
  add("line", yyjson_mut_int(doc, report.line));
  add("column", yyjson_mut_int(doc, report.column));
  add("message", String(doc, report.message));
  add("checker_name", String(doc, report.checker_name));
  add("severity", OptionalString(doc, report.severity));
  add("report_hash", OptionalString(doc, report.report_hash));
  add("analyzer_name", OptionalString(doc, report.analyzer_name));
  add("category", OptionalString(doc, report.category));
  add("type", yyjson_mut_null(doc));
  add("review_status", String(doc, "unreviewed"));
  yyjson_mut_val* events = yyjson_mut_arr(doc);
  for (const BugPathEvent& event : report.bug_path_events) {
    yyjson_mut_arr_append(events, EventToJson(event, doc));
  }
  add("bug_path_events", events);
  yyjson_mut_val* positions = yyjson_mut_arr(doc);
  for (const BugPathPosition& position : report.bug_path_positions) {
    yyjson_mut_val* item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, item, "range",
                           RangeToJson(position.range, doc));
    yyjson_mut_obj_add_val(doc, item, "file", FileToJson(position.file, doc));
    yyjson_mut_arr_append(positions, item);
  }
  add("bug_path_positions", positions);
  yyjson_mut_val* notes = yyjson_mut_arr(doc);
  for (const BugPathEvent& note : report.notes) {
    yyjson_mut_arr_append(notes, EventToJson(note, doc));
  }
  add("notes", notes);
  yyjson_mut_val* macros = yyjson_mut_arr(doc);
  for (const MacroExpansion& macro : report.macro_expansions) {
    yyjson_mut_arr_append(macros, EventToJson(macro.event, doc, &macro.name));
  }
  add("macro_expansions", macros);
  yyjson_mut_val* annotations = yyjson_mut_obj(doc);
  for (const auto& [key, value] : report.annotations) {
    yyjson_mut_obj_add(annotations, String(doc, key), String(doc, value));
  }
  add("annotations", annotations);
  return object;
}

std::string GetLine(const std::string& path, int line) {
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    kLog.Error("Failed to open file " + path);
    return "";
  }
  if (line <= 0) return "";
  const std::string text =
      util::TranslateNewlines(util::DecodeUtf8Ignore(content.value()));
  const std::vector<std::string_view> lines = util::SplitLines(text);
  if (static_cast<size_t>(line) > lines.size()) return "";
  return std::string(lines[line - 1]);
}

}  // namespace cc::report
