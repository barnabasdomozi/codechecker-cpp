// Port of codechecker_report_converter/report/__init__.py: the report model
// that every command uses (report-format.md §1).

#ifndef CC_REPORT_MODEL_REPORT_H_
#define CC_REPORT_MODEL_REPORT_H_

#include <yyjson.h>

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cc::report {

// A source file of a report. Two files are equal if their ids are equal.
struct File {
  std::string id;
  std::string path;  // The path, trimmed by --trim-path-prefix.
  std::string original_path;
};

// Returns the File at `path`, with that path as its id.
File MakeFile(const std::string& path);

bool operator==(const File& a, const File& b);

// A range of source text; lines and columns start at 1.
struct Range {
  int start_line;
  int start_col;
  int end_line;
  int end_col;

  friend bool operator==(const Range& a, const Range& b) = default;
};

// A point of the control flow of a report (an arrow end in the viewers).
struct BugPathPosition {
  File file;
  std::optional<Range> range;
};

// An event of the bug path, or a note.
struct BugPathEvent {
  std::string message;
  File file;
  int line;
  int column;
  std::optional<Range> range = std::nullopt;

  friend bool operator==(const BugPathEvent& a,
                         const BugPathEvent& b) = default;
};

// A macro expansion; the event's message is the expanded text.
struct MacroExpansion {
  BugPathEvent event;
  std::string name;
};

// One finding of an analyzer.
struct Report {
  std::optional<std::string> analyzer_result_file_path = {};
  File file = {};
  int line = 0;
  int column = 0;
  std::string message = {};
  std::string checker_name = {};
  std::optional<std::string> severity = {};
  std::optional<std::string> report_hash = {};
  std::optional<std::string> analyzer_name = {};
  std::optional<std::string> category = {};
  std::vector<BugPathEvent> bug_path_events = {};
  std::vector<BugPathPosition> bug_path_positions = {};
  std::vector<BugPathEvent> notes = {};
  std::vector<MacroExpansion> macro_expansions = {};
  // The report annotations, in their order (empty: none).
  std::vector<std::pair<std::string, std::string>> annotations = {};
  // The message used for hashing; converters may leave out parts that change
  // from run to run, such as addresses.
  std::string static_message = {};
};

// Returns a report like Python's Report(file, line, column, message,
// checker_name, bug_path_events=events): without `events`, the bug path is
// one event with the message at the report's location.
Report MakeReport(File file, int line, int column, std::string message,
                  std::string checker_name,
                  std::optional<std::vector<BugPathEvent>> events =
                      std::nullopt);

// Returns the original paths of every file `report` refers to: its own, and
// those of its events, positions, notes and macro expansions.
std::set<std::string> OriginalFiles(const Report& report);

// Returns `report` as Report.to_json() builds it, in `document`.
yyjson_mut_val* ReportToJson(const Report& report, yyjson_mut_doc* document);

// Returns line `line` of the file at `path`, with its line terminator, read
// as UTF-8 with undecodable bytes dropped and universal newlines; "" if the
// file has fewer lines or cannot be read, which is logged (Python:
// util.get_line()).
std::string GetLine(const std::string& path, int line);

}  // namespace cc::report

#endif  // CC_REPORT_MODEL_REPORT_H_
