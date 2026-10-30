#include "report/output/plaintext.h"

#include <stddef.h>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

void Write(std::string_view text) { util::Write(util::Stream::kStdout, text); }

// Returns the key that orders file paths (Python: str.casefold). Only ASCII
// letters are folded; folding the others comes with the first test that
// needs it.
std::string CaseFold(std::string_view text) { return util::AsciiLower(text); }

// Returns the source line of `report` with a "^" under its column (Python:
// format_main_report()); "" if the line cannot be read.
std::string FormatMainReport(const Report& report, SourceFileCache& sources) {
  const std::string_view line =
      sources.Line(report.file.original_path, report.line);
  if (line.empty()) return "";
  const std::string_view before_column =
      util::CodePointSlicePrefix(line, report.column - 1);
  // Tabs are printed as two spaces.
  const size_t marker_width =
      util::CodePointCount(before_column) +
      static_cast<size_t>(std::ranges::count(before_column, '\t'));
  return util::Replace(line, "\t", "  ") + std::string(marker_width, ' ') +
         "^";
}

// Python: format_report().
std::string FormatReport(const Report& report, bool content_is_not_changed) {
  // Python fails on a report without events; the report's own file is used
  // for those.
  const std::string& file_path = report.bug_path_events.empty()
                                     ? report.file.path
                                     : report.bug_path_events.back().file.path;
  std::string out = std::format(
      "[{}] {}:{}:{}: {} [{}]", report.severity.value_or("None"), file_path,
      report.line, report.column, report.message, report.checker_name);
  const std::string& status = report.review_status.status;
  if (content_is_not_changed && status != "unreviewed") {
    out += " [" + FormattedStatus(report.review_status) + "]";
  }
  return out;
}

// Python: format_event() and format_note().
std::string FormatEvent(const BugPathEvent& event) {
  return std::format("{}:{}:{}: {}", util::pypath::Basename(event.file.path),
                     event.line, event.column, event.message);
}

// Python: format_macro_expansion().
std::string FormatMacroExpansion(const MacroExpansion& macro) {
  return std::format("{}:{}:{}: Macro '{}' expanded to '{}'",
                     util::pypath::Basename(macro.event.file.path),
                     macro.event.line, macro.event.column, macro.name,
                     macro.event.message);
}

// Writes `title` and the numbered `lines`, the numbers right-aligned to the
// width of the largest one (Python: get_index_format()).
void WriteNumbered(std::string_view title,
                   const std::vector<std::string>& lines) {
  Write(title);
  const size_t width = std::to_string(lines.size()).size();
  for (size_t i = 0; i < lines.size(); ++i) {
    Write(std::format("    {:>{}}, {}\n", i + 1, width, lines[i]));
  }
}

// Python: print_details().
void PrintDetails(const Report& report) {
  Write("  Report hash: " + report.report_hash.value_or("None") + "\n");
  if (!report.macro_expansions.empty()) {
    std::vector<std::string> lines;
    for (const MacroExpansion& macro : report.macro_expansions) {
      lines.push_back(FormatMacroExpansion(macro));
    }
    WriteNumbered("  Macro expansions:\n", lines);
  }
  if (!report.notes.empty()) {
    std::vector<std::string> lines;
    for (const BugPathEvent& note : report.notes) {
      lines.push_back(FormatEvent(note));
    }
    WriteNumbered("  Notes:\n", lines);
  }
  std::vector<std::string> lines;
  for (const BugPathEvent& event : report.bug_path_events) {
    lines.push_back(FormatEvent(event));
  }
  WriteNumbered("  Steps:\n", lines);
}

// Prints one report and what belongs to it, its source line read through
// `sources`.
void PrintReport(const Report& report, bool print_steps,
                 SourceFileCache& sources) {
  // A changed file has other comments and lines than the analyzed one.
  const bool content_is_not_changed =
      !ChangedFiles(report).contains(report.file.original_path);
  Write(FormatReport(report, content_is_not_changed) + "\n");
  if (content_is_not_changed) {
    const std::string& comment = report.review_status.source_comment;
    if (!comment.empty()) {
      Write(std::string(util::RightStrip(comment)) + "\n");
    }
    Write(FormatMainReport(report, sources));
  } else {
    Write(kInvalidFileContentMessage);
  }
  Write("\n");
  if (print_steps) PrintDetails(report);
  Write("\n");
  util::Flush(util::Stream::kStdout);
}

}  // namespace

FileReportMap GetFileReportMap(const std::vector<Report>& reports,
                               const std::optional<std::string>& source_file) {
  FileReportMap map;
  const auto entry = [&map](const std::string& path) {
    const auto it = std::ranges::find(map, path, &FileReports::path);
    return it != map.end()
               ? it
               : map.insert(map.end(), {.path = path, .reports = {}});
  };
  for (const Report& report : reports) {
    entry(report.file.path)->reports.push_back(&report);
  }
  if (source_file) entry(*source_file);
  return map;
}

void PrintReports(const FileReportMap& file_report_map, bool print_steps,
                  SourceFileCache& sources,
                  std::set<std::string>* processed_file_paths) {
  std::vector<const FileReports*> files;
  for (const FileReports& file : file_report_map) files.push_back(&file);
  std::ranges::stable_sort(files, {}, [](const FileReports* file) {
    return CaseFold(file->path);
  });
  for (const FileReports* file : files) {
    const std::string& path = file->path;
    std::vector<const Report*> sorted = file->reports;
    std::ranges::stable_sort(sorted, {},
                             [](const Report* report) { return report->line; });
    for (const Report* report : sorted) {
      PrintReport(*report, print_steps, sources);
    }
    const std::string name = util::pypath::Basename(path);
    if (!sorted.empty()) {
      Write(std::format("Found {} defect(s) in {}\n\n", sorted.size(), name));
    } else if (!processed_file_paths->contains(path)) {
      Write("Found no defects in " + name + "\n");
    }
    processed_file_paths->insert(path);
  }
}

}  // namespace cc::report
