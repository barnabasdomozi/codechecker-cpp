#include "report/output/html.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/model/report.h"
#include "report/output/checker_info.h"
#include "report/output/statistics.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/template.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

// A template placeholder and the static file, relative to the layout
// folder, that fills it.
struct LayoutFile {
  const char* tag;
  const char* path;
};

constexpr LayoutFile kLayoutFiles[] = {
    {.tag = "style_css", .path = "css/style.css"},
    {.tag = "buglist_css", .path = "css/buglist.css"},
    {.tag = "bugview_css", .path = "css/bugview.css"},
    {.tag = "statistics_css", .path = "css/statistics.css"},
    {.tag = "icon_css", .path = "css/icon.css"},
    {.tag = "table_css", .path = "css/table.css"},
    {.tag = "codemirror_license",
     .path = "vendor/codemirror/codemirror.LICENSE"},
    {.tag = "codemirror_css", .path = "vendor/codemirror/codemirror.min.css"},
    {.tag = "codemirror_js", .path = "vendor/codemirror/codemirror.min.js"},
    {.tag = "clike_js", .path = "vendor/codemirror/clike.min.js"},
    {.tag = "bug_viewer", .path = "js/bugviewer.js"},
    {.tag = "bug_list", .path = "js/buglist.js"},
    {.tag = "browser_support", .path = "js/browsersupport.js"}};

// The severities in the order of the statistics page.
constexpr std::string_view kSeverityOrder[] = {
    "CRITICAL", "HIGH", "MEDIUM", "LOW", "STYLE", "UNSPECIFIED"};

// Returns the text of the file at `path` as Python's open(path,
// encoding='utf-8', errors='replace').read() reads it (Python:
// get_file_content()).
Result<std::string> ReadText(const std::string& path) {
  const Result<std::string> bytes = util::ReadFile(path);
  if (!bytes.ok()) return bytes;
  return Result<std::string>(
      util::TranslateNewlines(util::DecodeUtf8Replace(bytes.value())));
}

// Returns the value of the annotation `key` of `report`, if it has one.
std::optional<std::string> Annotation(const Report& report,
                                      std::string_view key) {
  const auto it = std::ranges::find_if(
      report.annotations,
      [key](const auto& annotation) { return annotation.first == key; });
  if (it == report.annotations.end()) return std::nullopt;
  return it->second;
}

// Returns `event` as the pages show it: {message, [name,] fileId, line,
// column}. `name` is the macro name of a macro expansion, else null.
yyjson_mut_val* EventToJson(yyjson_mut_doc* doc, const BugPathEvent& event,
                            const std::string* name) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, object, "message",
                         util::MakeJsonString(doc, event.message));
  if (name != nullptr) {
    yyjson_mut_obj_add_val(doc, object, "name",
                           util::MakeJsonString(doc, *name));
  }
  yyjson_mut_obj_add_val(doc, object, "fileId",
                         util::MakeJsonString(doc, event.file.id));
  yyjson_mut_obj_add_int(doc, object, "line", event.line);
  yyjson_mut_obj_add_int(doc, object, "column", event.column);
  return object;
}

// Returns the row of `checker` in the checker table of the statistics page.
std::string CheckerRow(const std::string& checker, const std::string& severity,
                       int count) {
  const std::string level = util::AsciiLower(severity);
  return std::format(
      "\n    <tr>\n    <td>{}</td>\n"
      "    <td class=\"severity\" severity=\"{}\">\n"
      "        <i class=\"severity-{}\" title=\"{}\"></i>\n"
      "    </td>\n    <td>{}</td>\n    </tr>\n",
      checker, level, level, level, count);
}

// Returns the row of `severity` in the severity table of the statistics
// page.
std::string SeverityRow(const std::string& severity, int count) {
  const std::string level = util::AsciiLower(severity);
  return std::format(
      "\n    <tr>\n"
      "    <td class=\"severity\" severity=\"{}\">\n"
      "        <i class=\"severity-{}\" title=\"{}\"></i>\n"
      "    </td>\n    <td>{}</td>\n    </tr>\n",
      level, level, level, count);
}

// Returns the position of `severity` in kSeverityOrder; unknown severities
// come last.
size_t SeverityRank(const std::string& severity) {
  return static_cast<size_t>(
      std::ranges::find(kSeverityOrder, severity) - std::begin(kSeverityOrder));
}

}  // namespace

Result<HtmlBuilder> HtmlBuilder::Create(const std::string& layout_dir,
                                        CheckerInfoLookup checker_info) {
  HtmlBuilder builder(std::move(checker_info));
  const std::pair<std::string*, const char*> templates[] = {
      {&builder.layout_template_, "layout.html"},
      {&builder.index_template_, "index.html"},
      {&builder.statistics_template_, "statistics.html"}};
  for (const auto& [content, name] : templates) {
    Result<std::string> text = ReadText(util::pypath::Join({layout_dir, name}));
    if (!text.ok()) return Result<HtmlBuilder>(text.error());
    *content = std::move(text).value();
  }
  for (const LayoutFile& file : kLayoutFiles) {
    Result<std::string> text =
        ReadText(util::pypath::Join({layout_dir, file.path}));
    if (!text.ok()) return Result<HtmlBuilder>(text.error());
    builder.values_[file.tag] = std::move(text).value();
  }
  return Result<HtmlBuilder>(std::move(builder));
}

std::string HtmlBuilder::Severity(const std::string& checker) const {
  return checker_info_ ? checker_info_(checker, std::nullopt).severity
                       : "UNSPECIFIED";
}

const HtmlBuilder::FileSource& HtmlBuilder::AddSourceFile(const File& file) {
  if (const auto it = files_.find(file.id); it != files_.end()) {
    return it->second;
  }
  const Result<std::string> content = ReadText(file.original_path);
  return files_
      .emplace(file.id,
               FileSource{.path = file.path,
                          .content = util::HtmlEscape(
                              content.ok() ? content.value()
                                           : kInvalidFileContentMessage)})
      .first->second;
}

HtmlBuilder::ListedReport HtmlBuilder::ListReport(const Report& report) const {
  ListedReport listed{
      .path = report.file.path,
      .report_hash = report.report_hash,
      .checker_name = report.checker_name,
      .checker_url = "",
      .line = report.line,
      .message = report.message,
      .review_status = FormattedStatus(report.review_status),
      .severity = Severity(report.checker_name),
      .bug_path_length = static_cast<int>(report.bug_path_events.size()),
      .testcase = Annotation(report, "testcase"),
      .timestamp = Annotation(report, "timestamp"),
      .chronological_order = Annotation(report, "chronological_order")};
  if (checker_info_) {
    listed.checker_url =
        checker_info_(report.checker_name, report.analyzer_name)
            .doc_url.value_or("");
  }
  return listed;
}

void HtmlBuilder::AddFileJson(yyjson_mut_doc* doc, yyjson_mut_val* files,
                              const File& file) {
  if (yyjson_mut_obj_getn(files, file.id.data(), file.id.size()) !=
      nullptr) {
    return;
  }
  const FileSource& source = AddSourceFile(file);
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, object, "id",
                         util::MakeJsonString(doc, file.id));
  yyjson_mut_obj_add_val(doc, object, "filePath",
                         util::MakeJsonString(doc, source.path));
  yyjson_mut_obj_add_val(doc, object, "content",
                         util::MakeJsonString(doc, source.content));
  yyjson_mut_obj_add(files, util::MakeJsonString(doc, file.id), object);
}

yyjson_mut_val* HtmlBuilder::EventsJson(
    yyjson_mut_doc* doc, yyjson_mut_val* files,
    const std::vector<BugPathEvent>& events) {
  yyjson_mut_val* array = yyjson_mut_arr(doc);
  for (const BugPathEvent& event : events) {
    AddFileJson(doc, files, event.file);
    yyjson_mut_arr_append(array, EventToJson(doc, event, nullptr));
  }
  return array;
}

yyjson_mut_val* HtmlBuilder::ReportJson(yyjson_mut_doc* doc,
                                        yyjson_mut_val* files,
                                        const Report& report,
                                        const ListedReport& listed) {
  // The files are added in the order Python adds them: the report's, then
  // those of the events, the macro expansions and the notes.
  AddFileJson(doc, files, report.file);
  yyjson_mut_val* checker = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, checker, "name",
                         util::MakeJsonString(doc, listed.checker_name));
  yyjson_mut_obj_add_val(doc, checker, "url",
                         util::MakeJsonString(doc, listed.checker_url));
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  const auto add = [doc, object](const char* key, yyjson_mut_val* value) {
    yyjson_mut_obj_add_val(doc, object, key, value);
  };
  add("fileId", util::MakeJsonString(doc, report.file.id));
  add("reportHash", util::MakeJsonOptionalString(doc, listed.report_hash));
  add("path", util::MakeJsonString(doc, listed.path));
  add("checker", checker);
  add("analyzerName", util::MakeJsonOptionalString(doc, report.analyzer_name));
  add("line", yyjson_mut_int(doc, report.line));
  add("column", yyjson_mut_int(doc, report.column));
  add("message", util::MakeJsonString(doc, listed.message));
  add("events", EventsJson(doc, files, report.bug_path_events));
  yyjson_mut_val* macros = yyjson_mut_arr(doc);
  for (const MacroExpansion& macro : report.macro_expansions) {
    AddFileJson(doc, files, macro.event.file);
    yyjson_mut_arr_append(macros, EventToJson(doc, macro.event, &macro.name));
  }
  add("macros", macros);
  add("notes", EventsJson(doc, files, report.notes));
  add("reviewStatus", util::MakeJsonString(doc, listed.review_status));
  add("severity", util::MakeJsonString(doc, listed.severity));
  add("testcase", util::MakeJsonOptionalString(doc, listed.testcase));
  add("timestamp", util::MakeJsonOptionalString(doc, listed.timestamp));
  add("chronologicalOrder",
      util::MakeJsonOptionalString(doc, listed.chronological_order));
  return object;
}

Result<void> HtmlBuilder::WritePage(const std::string& page_template,
                                    const std::string& path) const {
  const Result<std::string> content =
      util::SubstituteTemplate(page_template, values_);
  if (!content.ok()) return Result<void>(content.error());
  return util::WriteFile(path, content.value());
}

Result<void> HtmlBuilder::WriteIndex(const std::string& path) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* rows = yyjson_mut_arr(doc);
  for (const Page& page : pages_) {
    const std::string link = util::pypath::Basename(page.path);
    for (const ListedReport& report : page.reports) {
      yyjson_mut_val* row = yyjson_mut_obj(doc);
      const auto add = [doc, row](const char* key, yyjson_mut_val* value) {
        yyjson_mut_obj_add_val(doc, row, key, value);
      };
      add("link", util::MakeJsonString(doc, link));
      add("file-path", util::MakeJsonString(doc, report.path));
      add("report-hash", util::MakeJsonOptionalString(doc, report.report_hash));
      add("checker-name", util::MakeJsonString(doc, report.checker_name));
      add("checker-url", util::MakeJsonString(doc, report.checker_url));
      add("line", yyjson_mut_int(doc, report.line));
      add("message", util::MakeJsonString(doc, report.message));
      add("review-status", util::MakeJsonString(doc, report.review_status));
      add("severity", util::MakeJsonString(doc, report.severity));
      add("bug-path-length", yyjson_mut_int(doc, report.bug_path_length));
      add("testcase", util::MakeJsonOptionalString(doc, report.testcase));
      add("timestamp", util::MakeJsonOptionalString(doc, report.timestamp));
      add("chronological-order",
          util::MakeJsonOptionalString(doc, report.chronological_order));
      yyjson_mut_arr_append(rows, row);
    }
  }
  values_["table_reports"] = util::DumpJson(rows);
  return WritePage(index_template_, path);
}

Result<void> HtmlBuilder::WriteStatistics(const std::string& path) {
  int report_count = 0;
  std::map<std::string, int> checker_counts;
  for (const Page& page : pages_) {
    for (const ListedReport& report : page.reports) {
      ++report_count;
      ++checker_counts[report.checker_name];
    }
  }
  std::string checker_rows;
  std::vector<std::pair<std::string, int>> severity_counts;
  for (const auto& [checker, count] : checker_counts) {
    const std::string severity = Severity(checker);
    checker_rows += CheckerRow(checker, severity, count);
    const auto it = std::ranges::find(
        severity_counts, severity, &std::pair<std::string, int>::first);
    if (it != severity_counts.end()) {
      it->second += count;
    } else {
      severity_counts.emplace_back(severity, count);
    }
  }
  std::ranges::stable_sort(severity_counts, {}, [](const auto& entry) {
    return SeverityRank(entry.first);
  });
  std::string severity_rows;
  for (const auto& [severity, count] : severity_counts) {
    severity_rows += SeverityRow(severity, count);
  }
  values_["num_of_analyzer_result_files"] = std::to_string(pages_.size());
  values_["number_of_reports"] = std::to_string(report_count);
  values_["checker_statistics"] = checker_rows;
  values_["severity_statistics"] = severity_rows;
  return WritePage(statistics_template_, path);
}

Result<void> HtmlBuilder::Convert(const std::string& result_file,
                                  const std::vector<Report>& reports,
                                  const std::string& output_dir) {
  if (reports.empty()) {
    kLog.Info(std::format("No report data in {} file.", result_file));
    return Result<void>();
  }
  const std::string path = util::pypath::Join(
      {output_dir, util::pypath::Basename(result_file) + ".html"});
  for (const Report& report : reports) {
    if (!ChangedFiles(report).empty()) return Result<void>();
  }

  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* files = yyjson_mut_obj(doc);
  yyjson_mut_val* html_reports = yyjson_mut_arr(doc);
  Page page{.path = path, .reports = {}};
  for (const Report& report : reports) {
    ListedReport listed = ListReport(report);
    yyjson_mut_arr_append(html_reports, ReportJson(doc, files, report, listed));
    page.reports.push_back(std::move(listed));
  }
  yyjson_mut_val* data = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, data, "files", files);
  yyjson_mut_obj_add_val(doc, data, "reports", html_reports);
  values_["report_data"] = util::DumpJson(data);
  // A page written again keeps its place in the list, as in Python's dict.
  if (const auto it = std::ranges::find(pages_, path, &Page::path);
      it != pages_.end()) {
    *it = std::move(page);
  } else {
    pages_.push_back(std::move(page));
  }
  if (Result<void> written = WritePage(layout_template_, path);
      !written.ok()) {
    return written;
  }
  kLog.Info("Html file was generated: " + path);
  return Result<void>();
}

Result<void> HtmlBuilder::Finish(const std::string& output_dir,
                                 const Statistics& statistics) {
  const std::string index_path =
      util::pypath::Join({output_dir, "index.html"});
  if (Result<void> written = WriteIndex(index_path); !written.ok()) {
    return written;
  }
  const std::string statistics_path =
      util::pypath::Join({output_dir, "statistics.html"});
  if (Result<void> written = WriteStatistics(statistics_path);
      !written.ok()) {
    return written;
  }
  statistics.Write();
  util::Print("\nTo view statistics in a browser run:\n> firefox " +
              statistics_path);
  util::Print("\nTo view the results in a browser run:\n> firefox " +
              index_path);
  return Result<void>();
}

}  // namespace cc::report
