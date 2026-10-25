#include "report/converter/clang_tidy.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/converter/line_parser.h"
#include "report/model/report.h"
#include "util/file.h"
#include "util/json.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/yaml.h"

namespace cc::report {
namespace {

const util::Regex& MessageLine() {
  static const util::Regex& regex = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): )"
      R"((?P<severity>(error|warning)):(?P<message>[\S \t]+)\s*)"
      R"(\[(?P<checker>.*)\])");
  return regex;
}

const util::Regex& NoteLine() {
  static const util::Regex& regex = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): )"
      R"(note:(?P<message>.*))");
  return regex;
}

// Returns the category of a clang-tidy checker: its name up to the first
// "-".
std::string Category(std::string_view checker_name) {
  return std::string(checker_name.substr(0, checker_name.find('-')));
}

// Returns the number of code points before byte `pos` of `text`, or -1 for
// npos (Python: str.find()).
int64_t CodePointIndex(std::string_view text, size_t pos) {
  if (pos == std::string_view::npos) return -1;
  return static_cast<int64_t>(util::CodePointCount(text.substr(0, pos)));
}

// Skips the source line and the caret line after a diagnostic or note.
// Returns the next line, or nullopt at the end (Python: _parse_code()).
std::optional<std::string> ParseCode(LineReader* reader,
                                     const std::string& line) {
  if (NoteLine().Match(line) || MessageLine().Match(line)) return line;
  std::optional<std::string> caret = reader->Next();
  if (!caret || caret->find('^') == std::string::npos) return caret;
  return reader->Next();
}

// Adds the fix-it lines as notes (Python: _parse_fixits()).
Result<std::optional<std::string>> ParseFixits(Report* report,
                                               LineReader* reader,
                                               std::string line) {
  static const util::Regex& kNewStyle =
      *new util::Regex(R"(^\s*\d*\s*\|\s*(?P<message>\S.*))");
  static const util::Regex& kOldStyle =
      *new util::Regex(R"(^\s*(?P<message>\S.*))");
  using Next = Result<std::optional<std::string>>;
  while (!MessageLine().Match(line) && !NoteLine().Match(line)) {
    std::string text;
    int64_t column = 0;
    if (const std::optional<util::RegexMatch> match = kNewStyle.Match(line)) {
      // Since Clang 17: whitespace, maybe a line number, and "|".
      text = std::string(match->Group("message").value_or(""));
      column = CodePointIndex(line, line.find(text)) -
               CodePointIndex(line, line.find('|')) - 1;
    } else if (const std::optional<util::RegexMatch> old_match =
                   kOldStyle.Match(line)) {
      text = std::string(old_match->Group("message").value_or(""));
      column = CodePointIndex(line, line.find(text)) + 1;
    } else {
      return Next(Error{.message = "ValueError: Unexpected line: " + line +
                                   ". Expected a FixIt line!"});
    }
    report->notes.push_back({.message = text + " (fixit)",
                             .file = report->file,
                             .line = report->line,
                             .column = static_cast<int>(column)});
    std::optional<std::string> next = reader->Next();
    if (!next) return Next(std::nullopt);
    line = std::move(*next);
  }
  return Next(line);
}

// Adds the notes as events (Python: _parse_notes()).
std::optional<std::string> ParseNotes(Report* report, LineReader* reader,
                                      std::string line) {
  while (!MessageLine().Match(line)) {
    const std::optional<util::RegexMatch> match = NoteLine().Match(line);
    if (!match) return reader->Next();
    report->bug_path_events.push_back(
        {.message = std::string(
             util::Strip(match->Group("message").value_or(""))),
         .file = MakeFile(
             util::pypath::Abspath(match->Group("path").value_or(""))),
         .line = ToInt(match->Group("line").value_or("")),
         .column = ToInt(match->Group("column").value_or(""))});
    std::optional<std::string> next = reader->Next();
    if (next) next = ParseCode(reader, *next);
    if (!next) return std::nullopt;
    line = std::move(*next);
  }
  return line;
}

// Python: clang_tidy.Parser._parse_line().
Result<std::optional<std::string>> ParseLine(LineReader* reader,
                                             const std::string& line,
                                             std::vector<Report>* reports) {
  using Next = Result<std::optional<std::string>>;
  const std::optional<util::RegexMatch> match = MessageLine().Match(line);
  if (!match) return Next(reader->Next());
  std::vector<std::string> checker_names;
  const std::string_view checkers =
      util::Strip(match->Group("checker").value_or(""));
  for (size_t start = 0;;) {
    const size_t comma = checkers.find(',', start);
    checker_names.emplace_back(checkers.substr(start, comma - start));
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  Report report = MakeReport(
      MakeFile(util::pypath::Abspath(match->Group("path").value_or(""))),
      ToInt(match->Group("line").value_or("")),
      ToInt(match->Group("column").value_or("")),
      std::string(util::Strip(match->Group("message").value_or(""))),
      checker_names.front(), std::vector<BugPathEvent>());

  std::optional<std::string> next = reader->Next();
  if (next) next = ParseCode(reader, *next);
  if (next) {
    Next fixits = ParseFixits(&report, reader, *next);
    if (!fixits.ok()) return fixits;
    next = fixits.value();
  }
  if (next) next = ParseNotes(&report, reader, *next);

  report.bug_path_events.push_back({.message = report.message,
                                    .file = report.file,
                                    .line = report.line,
                                    .column = report.column});
  // A checker and its alias, both enabled, give one diagnostic with both
  // names; each gets a report.
  for (const std::string& checker_name : checker_names) {
    Report copy = report;
    copy.checker_name = checker_name;
    copy.category = Category(checker_name);
    reports->push_back(std::move(copy));
  }
  return Next(next.value_or(""));
}

// Returns the line and column at character `offset` of the file at `path`
// (Python: get_location_by_offset()).
Result<std::pair<int, int>> LocationByOffset(const std::string& path,
                                             yyjson_val* offset_value) {
  using Location = Result<std::pair<int, int>>;
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    return Location(Error{.message = "FileNotFoundError: " +
                                     content.error().message});
  }
  if (!yyjson_is_int(offset_value)) {
    return Location(Error{.message = "TypeError: '<' not supported between "
                                     "instances of 'int' and '" +
                                     std::string(yyjson_get_type_desc(
                                         offset_value)) +
                                     "'"});
  }
  int64_t offset = yyjson_get_sint(offset_value);
  const std::string text = util::DecodeUtf8Ignore(content.value());
  int row = 1;
  for (const std::string_view line : util::SplitLines(text)) {
    const int64_t length = static_cast<int64_t>(util::CodePointCount(line));
    if (length < offset) {
      offset -= length;
    } else {
      return Location(std::pair<int, int>(row, static_cast<int>(offset + 1)));
    }
    ++row;
  }
  return Location(Error{.message = "TypeError: cannot unpack non-iterable "
                                   "NoneType object"});
}

// Python: clang_tidy_yaml.Parser._parse_diagnostic().
Result<Report> ParseDiagnostic(yyjson_val* diagnostic) {
  yyjson_val* name = util::JsonGet(diagnostic, "DiagnosticName");
  const std::string checker_name = name != nullptr ? util::JsonToStr(name) : "";
  yyjson_val* message = util::JsonGet(diagnostic, "DiagnosticMessage");
  const Result<yyjson_val*> file_path =
      util::JsonSubscript(message, "FilePath");
  if (!file_path.ok()) return Result<Report>(file_path.error());
  const std::string path =
      util::pypath::Abspath(util::JsonToStr(file_path.value()));
  const Result<std::pair<int, int>> location =
      LocationByOffset(path, util::JsonGet(message, "FileOffset"));
  if (!location.ok()) return Result<Report>(location.error());
  yyjson_val* text = util::JsonGet(message, "Message");
  Report report = MakeReport(
      MakeFile(path), location.value().first, location.value().second,
      std::string(util::Strip(text != nullptr ? util::JsonToStr(text) : "")),
      checker_name, std::vector<BugPathEvent>());
  report.category = Category(checker_name);

  yyjson_arr_iter replacements =
      yyjson_arr_iter_with(util::JsonGet(message, "Replacements"));
  while (yyjson_val* replacement = yyjson_arr_iter_next(&replacements)) {
    const Result<yyjson_val*> replacement_path =
        util::JsonSubscript(replacement, "FilePath");
    const Result<yyjson_val*> offset =
        util::JsonSubscript(replacement, "Offset");
    if (!replacement_path.ok()) return Result<Report>(replacement_path.error());
    if (!offset.ok()) return Result<Report>(offset.error());
    const std::string fixit_path =
        util::pypath::Abspath(util::JsonToStr(replacement_path.value()));
    const Result<std::pair<int, int>> fixit_location =
        LocationByOffset(fixit_path, offset.value());
    if (!fixit_location.ok()) return Result<Report>(fixit_location.error());
    const Result<yyjson_val*> replacement_text =
        util::JsonSubscript(replacement, "ReplacementText");
    if (!replacement_text.ok()) return Result<Report>(replacement_text.error());
    report.notes.push_back(
        {.message = util::JsonToStr(replacement_text.value()) + " (fixit)",
         .file = MakeFile(fixit_path),
         .line = fixit_location.value().first,
         .column = fixit_location.value().second});
  }

  yyjson_arr_iter notes =
      yyjson_arr_iter_with(util::JsonGet(diagnostic, "Notes"));
  while (yyjson_val* note = yyjson_arr_iter_next(&notes)) {
    const Result<yyjson_val*> note_path = util::JsonSubscript(note, "FilePath");
    if (!note_path.ok()) return Result<Report>(note_path.error());
    if (util::JsonToStr(note_path.value()).empty()) continue;
    const std::string event_path =
        util::pypath::Abspath(util::JsonToStr(note_path.value()));
    const Result<yyjson_val*> offset = util::JsonSubscript(note, "FileOffset");
    if (!offset.ok()) return Result<Report>(offset.error());
    const Result<std::pair<int, int>> event_location =
        LocationByOffset(event_path, offset.value());
    if (!event_location.ok()) return Result<Report>(event_location.error());
    const Result<yyjson_val*> note_message =
        util::JsonSubscript(note, "Message");
    if (!note_message.ok()) return Result<Report>(note_message.error());
    report.bug_path_events.push_back(
        {.message =
             std::string(util::Strip(util::JsonToStr(note_message.value()))),
         .file = MakeFile(event_path),
         .line = event_location.value().first,
         .column = event_location.value().second});
  }

  report.bug_path_events.push_back({.message = report.message,
                                    .file = report.file,
                                    .line = report.line,
                                    .column = report.column});
  return Result<Report>(std::move(report));
}

// Returns whether Python's bool(value) is true.
bool Truthy(yyjson_val* value) {
  switch (yyjson_get_type(value)) {
    case YYJSON_TYPE_NULL:
      return false;
    case YYJSON_TYPE_BOOL:
      return yyjson_get_bool(value);
    case YYJSON_TYPE_NUM:
      return yyjson_is_real(value) ? yyjson_get_real(value) != 0
                                   : yyjson_get_sint(value) != 0;
    case YYJSON_TYPE_STR:
      return yyjson_get_len(value) > 0;
    case YYJSON_TYPE_ARR:
      return yyjson_arr_size(value) > 0;
    case YYJSON_TYPE_OBJ:
      return yyjson_obj_size(value) > 0;
    default:
      return false;
  }
}

}  // namespace

Result<std::vector<Report>> GetClangTidyReports(const std::string& path) {
  return ParseLines(path, ParseLine);
}

Result<std::vector<Report>> GetClangTidyYamlReports(const std::string& path) {
  using Reports = std::vector<Report>;
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) return Result<Reports>(content.error());
  const Result<util::JsonDocument> document =
      util::SafeLoadYaml(content.value(), path);
  if (!document.ok()) return Result<Reports>(document.error());
  Reports reports;
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  if (!Truthy(root)) return Result<Reports>(std::move(reports));
  const Result<yyjson_val*> diagnostics =
      util::JsonSubscript(root, "Diagnostics");
  if (!diagnostics.ok()) return Result<Reports>(diagnostics.error());
  yyjson_arr_iter iter = yyjson_arr_iter_with(diagnostics.value());
  while (yyjson_val* diagnostic = yyjson_arr_iter_next(&iter)) {
    Result<Report> report = ParseDiagnostic(diagnostic);
    if (!report.ok()) return Result<Reports>(report.error());
    reports.push_back(std::move(report).value());
  }
  return Result<Reports>(std::move(reports));
}

}  // namespace cc::report
