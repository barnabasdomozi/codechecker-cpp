#include "report/model/report.h"

#include <yyjson.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "util/file.h"
#include "util/fnmatch.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

yyjson_mut_val* FileToJson(const File& file, yyjson_mut_doc* doc) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, object, "id", util::MakeJsonString(doc, file.id));
  yyjson_mut_obj_add_val(doc, object, "path",
                         util::MakeJsonString(doc, file.path));
  yyjson_mut_obj_add_val(doc, object, "original_path",
                         util::MakeJsonString(doc, file.original_path));
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
    yyjson_mut_obj_add_val(doc, object, "name",
                           util::MakeJsonString(doc, *macro_name));
  }
  yyjson_mut_obj_add_val(doc, object, "file", FileToJson(event.file, doc));
  yyjson_mut_obj_add_int(doc, object, "line", event.line);
  yyjson_mut_obj_add_int(doc, object, "column", event.column);
  yyjson_mut_obj_add_val(doc, object, "message",
                         util::MakeJsonString(doc, event.message));
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
      util::MakeJsonOptionalString(doc, report.analyzer_result_file_path));
  add("file", FileToJson(report.file, doc));
  add("line", yyjson_mut_int(doc, report.line));
  add("column", yyjson_mut_int(doc, report.column));
  add("message", util::MakeJsonString(doc, report.message));
  add("checker_name", util::MakeJsonString(doc, report.checker_name));
  add("severity", util::MakeJsonOptionalString(doc, report.severity));
  add("report_hash", util::MakeJsonOptionalString(doc, report.report_hash));
  add("analyzer_name", util::MakeJsonOptionalString(doc, report.analyzer_name));
  add("category", util::MakeJsonOptionalString(doc, report.category));
  add("type", yyjson_mut_null(doc));
  add("review_status", util::MakeJsonString(doc, report.review_status.status));
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
    yyjson_mut_obj_add(annotations, util::MakeJsonString(doc, key),
                       util::MakeJsonString(doc, value));
  }
  add("annotations", annotations);
  return object;
}

std::string ReportRepr(const Report& report) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  return util::DumpJson(ReportToJson(report, document.get()));
}

std::vector<File*> FilesOf(Report* report) {
  std::vector<File*> files = {&report->file};
  for (BugPathEvent& event : report->bug_path_events) {
    files.push_back(&event.file);
  }
  for (BugPathPosition& position : report->bug_path_positions) {
    files.push_back(&position.file);
  }
  for (BugPathEvent& note : report->notes) files.push_back(&note.file);
  for (MacroExpansion& macro : report->macro_expansions) {
    files.push_back(&macro.event.file);
  }
  return files;
}

std::set<std::string> ChangedFiles(const Report& report) {
  std::set<std::string> changed;
  if (!report.analyzer_result_file_path) {
    kLog.Warning("No analyzer result file path is set for report: " +
                 ReportRepr(report));
    return changed;
  }
  const std::string& result_file = *report.analyzer_result_file_path;
  // A modification time of 0 counts as unknown, as in Python.
  const std::optional<double> result_time =
      platform::ModificationTime(result_file);
  if (!result_time) changed.insert(result_file);
  for (const std::string& path : OriginalFiles(report)) {
    const std::optional<double> time = platform::ModificationTime(path);
    if (!time || *time == 0 || !result_time || *result_time == 0 ||
        *time > *result_time) {
      changed.insert(path);
    }
  }
  return changed;
}

void DumpChangedFiles(const std::set<std::string>& changed_files) {
  if (changed_files.empty()) return;
  std::vector<std::string> items;
  for (const std::string& path : changed_files) items.push_back(" - " + path);
  kLog.Warning(
      "The following source file contents changed or missing since the "
      "latest analysis:\n" +
      util::Join(items, "\n") +
      "\nPlease re-analyze your project to update the reports!");
}

std::vector<util::Regex> PathPrefixPatterns(
    const std::vector<std::string>& prefixes) {
  std::vector<util::Regex> patterns;
  for (std::string prefix : prefixes) {
    if (!prefix.ends_with('/')) prefix += '/';
    // fnmatch's "\Z" end is dropped, so the pattern matches a prefix.
    std::string pattern = util::FnmatchTranslate(prefix);
    pattern.resize(pattern.size() - 2);
    patterns.emplace_back(pattern);
  }
  return patterns;
}

std::string TrimPathPrefixes(const std::string& path,
                             const std::vector<util::Regex>& patterns) {
  std::string_view longest;
  for (const util::Regex& pattern : patterns) {
    if (const std::optional<util::RegexMatch> match = pattern.Match(path)) {
      const std::string_view matched = *match->Group(0);
      if (longest.size() < matched.size()) longest = matched;
    }
  }
  if (longest.empty() || longest == "/") return path;
  return path.substr(longest.size());
}

std::string FormattedStatus(const ReviewStatus& status) {
  std::string formatted = util::AsciiLower(status.status);
  std::ranges::replace(formatted, '_', ' ');
  if (!formatted.empty() && formatted[0] >= 'a' && formatted[0] <= 'z') {
    formatted[0] = static_cast<char>(formatted[0] - 'a' + 'A');
  }
  return formatted;
}

std::string SourceLine(const Report& report) {
  if (ChangedFiles(report).contains(report.file.original_path)) {
    return std::string(kInvalidFileContentMessage);
  }
  return GetLine(report.file.original_path, report.line);
}

std::string GetLine(const std::string& path, int line) {
  const Result<std::string> text = util::ReadTextFile(path);
  if (!text.ok()) {
    kLog.Error("Failed to open file " + path);
    return "";
  }
  if (line <= 0) return "";
  const std::vector<std::string_view> lines = util::SplitLines(text.value());
  if (static_cast<size_t>(line) > lines.size()) return "";
  return std::string(lines[line - 1]);
}

}  // namespace cc::report
