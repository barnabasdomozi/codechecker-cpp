#include "report/hash/report_hash.h"

#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "util/hash.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

// Returns `line` with all whitespace removed, and `column` moved left by the
// whitespace in line[:column] (Python: _remove_whitespace()). The slice
// counts code points, with Python's rules for negative columns.
std::string RemoveWhitespace(std::string_view line, int* column) {
  const std::string_view prefix = util::CodePointSlicePrefix(line, *column);
  const int removed = static_cast<int>(
      util::CodePointCount(prefix) -
      util::CodePointCount(util::RemoveWhitespace(prefix)));
  *column -= removed;
  return util::RemoveWhitespace(line);
}

std::vector<std::string> ContextFreeParts(const Report& report,
                                          SourceFileCache& sources) {
  int column = report.column;
  const std::string line_content = RemoveWhitespace(
      sources.Line(report.file.original_path, report.line), &column);
  if (line_content.empty() &&
      !util::pypath::IsFile(report.file.original_path)) {
    kLog.Error("Failed to include source line in the report hash.");
    kLog.Error(report.file.original_path + " does not exists!");
  }
  return {util::pypath::Basename(report.file.original_path),
          report.static_message, line_content, std::to_string(column),
          std::to_string(column)};
}

std::vector<std::string> PathSensitiveParts(const Report& report,
                                            SourceFileCache& sources) {
  if (report.bug_path_events.empty()) {
    kLog.Error("Hash generation failed!");
    kLog.Error("list index out of range");
    return {};
  }
  const BugPathEvent& event = report.bug_path_events.back();
  const std::string line_content(
      sources.Line(event.file.original_path, event.line));
  if (line_content.empty() &&
      !util::pypath::IsFile(report.file.original_path)) {
    kLog.Error("Failed to generate report hash. " +
               report.file.original_path + " does not exists!");
  }
  std::vector<std::string> parts = {
      util::pypath::Basename(report.file.original_path), report.checker_name,
      report.static_message, line_content, std::to_string(event.column),
      std::to_string(event.column)};
  for (const BugPathPosition& position : report.bug_path_positions) {
    if (position.range) {
      parts.push_back(std::to_string(position.range->start_col));
      parts.push_back(std::to_string(position.range->end_col));
    }
  }
  return parts;
}

}  // namespace

std::string GetReportHash(const Report& report, HashType type,
                          SourceFileCache& sources) {
  std::vector<std::string> parts;
  switch (type) {
    case HashType::kContextFree:
      parts = ContextFreeParts(report, sources);
      break;
    case HashType::kPathSensitive:
      parts = PathSensitiveParts(report, sources);
      break;
    case HashType::kDiagnosticMessage:
      parts = ContextFreeParts(report, sources);
      for (const BugPathEvent& event : report.bug_path_events) {
        parts.push_back(event.message);
      }
      break;
  }
  return util::Md5Hex(util::Join(parts, "|||"));
}

std::string GetReportPathHash(const Report& report) {
  std::string content;
  for (const BugPathEvent& event : report.bug_path_events) {
    content += std::to_string(event.line) + "|" +
               std::to_string(event.column) + "|" + event.message + "|" +
               event.file.path;
  }
  for (const auto& [key, value] : report.annotations) {
    content += "|" + key + "|" + value;
  }
  content += report.checker_name + report.report_hash.value_or("");
  if (content.empty()) {
    kLog.Error("Failed to generate report path hash: " + ReportRepr(report));
  }
  return util::Md5Hex(content);
}

}  // namespace cc::report
