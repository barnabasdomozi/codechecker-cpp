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

#include "util/pyre.h"

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

// The review status of a report (Python: SourceReviewStatus).
struct ReviewStatus {
  // unreviewed, confirmed, false_positive or intentional.
  std::string status = "unreviewed";
  std::string message = {};
  // The source code comment that gives the status, as it is written in the
  // file (its lines joined by spaces); empty if the status does not come
  // from a source code comment.
  std::string source_comment = {};
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
  ReviewStatus review_status = {};
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

// Returns the files `report` refers to, with repetitions, in the order
// Python's Report.trim_path_prefixes() visits them: its own, then those of
// its events, positions, notes and macro expansions.
std::vector<File*> FilesOf(Report* report);

// Returns the files of `report` (original paths) that changed or are
// missing since its result file was written, and the result file itself if
// it is missing (Python: Report.changed_files). A report without a result
// file is logged and has none.
std::set<std::string> ChangedFiles(const Report& report);

// Logs a warning that lists `changed_files`, if there are any (Python:
// reports.dump_changed_files()).
void DumpChangedFiles(const std::set<std::string>& changed_files);

// Returns the patterns that TrimPathPrefixes() removes, made from the shell
// patterns `prefixes`; a "/" is added to those that do not end with one.
std::vector<util::Regex> PathPrefixPatterns(
    const std::vector<std::string>& prefixes);

// Returns `path` without the longest prefix that one of `patterns` (from
// PathPrefixPatterns()) matches (Python: util.trim_path_prefixes()). A
// match of only "/" is not removed.
std::string TrimPathPrefixes(const std::string& path,
                             const std::vector<util::Regex>& patterns);

// Returns `report` as Report.to_json() builds it, in `document`.
yyjson_mut_val* ReportToJson(const Report& report, yyjson_mut_doc* document);

// Returns repr(report): ReportToJson() written by json.dumps().
std::string ReportRepr(const Report& report);

// Returns line `line` of the file at `path`, with its line terminator, read
// as UTF-8 with undecodable bytes dropped and universal newlines; "" if the
// file has fewer lines or cannot be read, which is logged (Python:
// util.get_line()).
std::string GetLine(const std::string& path, int line);

}  // namespace cc::report

#endif  // CC_REPORT_MODEL_REPORT_H_
