#include "report/converter/line_formats.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/converter/line_parser.h"
#include "report/model/report.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

// A format with one finding per matching line; other lines are skipped.
struct LineFormat {
  const util::Regex* regex;  // With "path", "line" and "message" groups.
  bool normalize_path;       // Python: os.path.normpath() on the path.
  // The groups of the column and the checker name; empty: column 0, and
  // the type's name as the checker.
  std::string_view column_group;
  std::string_view checker_group;
};

// Returns the source path in a finding of the output file `input`.
std::string SourcePath(const std::string& input, std::string_view path,
                       bool normalize) {
  const std::string joined = RelativeToInput(input, path);
  return normalize ? util::pypath::Normpath(joined) : joined;
}

Result<std::vector<Report>> GetLineFormatReports(const std::string& path,
                                                 const LineFormat& format) {
  return ParseLines(path, [&path, &format](LineReader* reader,
                                           const std::string& line,
                                           std::vector<Report>* reports) {
    const std::optional<util::RegexMatch> match = format.regex->Match(line);
    if (match) {
      reports->push_back(MakeReport(
          MakeFile(SourcePath(path, match->Group("path").value_or(""),
                              format.normalize_path)),
          ToInt(match->Group("line").value_or("")),
          format.column_group.empty()
              ? 0
              : ToInt(match->Group(format.column_group).value_or("")),
          std::string(util::Strip(match->Group("message").value_or(""))),
          format.checker_group.empty()
              ? ""
              : std::string(util::Strip(
                    match->Group(format.checker_group).value_or("")))));
    }
    return Result<std::optional<std::string>>(reader->Next());
  });
}

}  // namespace

Result<std::vector<Report>> GetCpplintReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):  (?P<message>[\S \t]+)  \[)"
      R"((?P<checker_name>\S+)\]\s)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = true,
                                     .column_group = "",
                                     .checker_group = "checker_name"});
}

Result<std::vector<Report>> GetPyflakesReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<message>[\S \t]+)\s*)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = false,
                                     .column_group = "",
                                     .checker_group = ""});
}

Result<std::vector<Report>> GetGolintReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): )"
      R"((?P<message>[\S \t]+)\s*)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = false,
                                     .column_group = "column",
                                     .checker_group = ""});
}

Result<std::vector<Report>> GetMarkdownlintReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?): (?P<checker>[\S]+) )"
      R"((?P<message>[\S \t]+)\s*)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = true,
                                     .column_group = "",
                                     .checker_group = "checker"});
}

Result<std::vector<Report>> GetSphinxReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>\/[\S ]+?):(?P<line>\d+?):\s(?P<message>[\S \t]+)\s*)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = true,
                                     .column_group = "",
                                     .checker_group = ""});
}

Result<std::vector<Report>> GetKernelDocReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>\.[\S ]+?):(?P<line>\d+?):\s(?P<message>[\S \t]+)\s*)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = true,
                                     .column_group = "",
                                     .checker_group = ""});
}

Result<std::vector<Report>> GetSmatchReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?)\s(?P<function_name>\S+)\s)"
      R"(\[(?P<checker_name>smatch\.\S+)\]\s(?P<message>[\S \t]+)\s*)");
  return GetLineFormatReports(path, {.regex = &kLine,
                                     .normalize_path = true,
                                     .column_group = "",
                                     .checker_group = "checker_name"});
}

Result<std::vector<Report>> GetCoccinelleReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>(\d+?))-(\d+?):)"
      R"((?P<message>[\S \t]+)\s*)");
  static const util::Regex& kChecker =
      *new util::Regex(R"(^Processing (?P<checker_name>[\S ]+)\.cocci$)");
  // The checker of the "Processing <name>.cocci" line before a report.
  std::string checker_name;
  return ParseLines(path, [&path, &checker_name](
                              LineReader* reader, const std::string& line,
                              std::vector<Report>* reports) {
    if (const std::optional<util::RegexMatch> checker = kChecker.Match(line)) {
      checker_name = "coccinelle." +
                     std::string(checker->Group("checker_name").value_or(""));
    }
    if (const std::optional<util::RegexMatch> match = kLine.Match(line)) {
      reports->push_back(MakeReport(
          MakeFile(SourcePath(path, match->Group("path").value_or(""), true)),
          ToInt(match->Group("line").value_or("")),
          ToInt(match->Group("column").value_or("")),
          std::string(util::Strip(match->Group("message").value_or(""))),
          checker_name));
    }
    return Result<std::optional<std::string>>(reader->Next());
  });
}

Result<std::vector<Report>> GetSparseReports(const std::string& path) {
  static const util::Regex& kLine = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?):)"
      R"((?P<message>[\S \t]+)\s*)");
  static const util::Regex& kNote = *new util::Regex(
      R"(^(?P<path>\.[\S ]+?):(?P<line>\d+?):(?P<column>\d+?):)"
      R"((?P<message>[\S \t]+)\s*)");
  return ParseLines(path, [&path](LineReader* reader, const std::string& line,
                                  std::vector<Report>* reports) {
    using Next = Result<std::optional<std::string>>;
    const std::optional<util::RegexMatch> match = kLine.Match(line);
    if (!match) return Next(reader->Next());
    Report report = MakeReport(
        MakeFile(SourcePath(path, match->Group("path").value_or(""), true)),
        ToInt(match->Group("line").value_or("")),
        ToInt(match->Group("column").value_or("")),
        std::string(util::Strip(match->Group("message").value_or(""))), "",
        std::vector<BugPathEvent>());
    // The following lines with a relative path are events of the report.
    std::optional<std::string> next = reader->Next();
    while (next) {
      const std::optional<util::RegexMatch> note = kNote.Match(*next);
      if (!note) break;
      report.bug_path_events.push_back(
          {.message =
               std::string(util::Strip(note->Group("message").value_or(""))),
           .file = MakeFile(
               SourcePath(path, note->Group("path").value_or(""), true)),
           .line = ToInt(note->Group("line").value_or("")),
           .column = ToInt(note->Group("column").value_or(""))});
      next = reader->Next();
    }
    report.bug_path_events.push_back({.message = report.message,
                                      .file = report.file,
                                      .line = report.line,
                                      .column = report.column});
    reports->push_back(std::move(report));
    return Next(next.value_or(""));
  });
}

}  // namespace cc::report
