// Port of codechecker_report_converter/analyzers/parser.py: reading text
// analyzer output line by line (report-converter.md §3).

#ifndef CC_REPORT_CONVERTER_LINE_PARSER_H_
#define CC_REPORT_CONVERTER_LINE_PARSER_H_

#include <stddef.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// The lines of a text input, taken one after the other (Python: an iterator
// over the lines).
class LineReader {
 public:
  explicit LineReader(std::vector<std::string> lines)
      : lines_(std::move(lines)) {}

  LineReader(const LineReader&) = delete;
  LineReader& operator=(const LineReader&) = delete;

  // Returns the next line, or nullopt at the end (Python: next(it), which
  // raises StopIteration at the end).
  std::optional<std::string> Next() {
    if (next_ == lines_.size()) return std::nullopt;
    return lines_[next_++];
  }
  // Returns the next line, or "" at the end (Python: get_next(it)).
  std::string NextOrEmpty() { return Next().value_or(""); }

 private:
  std::vector<std::string> lines_;
  size_t next_ = 0;
};

// Parses `line`, the current line, appends the reports it finds, and
// returns the next line to parse, or nullopt at the end of the input
// (Python: BaseParser._parse_line()). It may take more lines from `reader`.
using ParseLineFunction = std::function<Result<std::optional<std::string>>(
    LineReader* reader, const std::string& line, std::vector<Report>* reports)>;

// Returns the reports that `parse_line` finds in the text file at `path`
// (Python: BaseParser.get_reports()). The file is read as UTF-8 with
// undecodable bytes replaced and universal newlines, and split into lines
// that keep their "\n".
Result<std::vector<Report>> ParseLines(const std::string& path,
                                       const ParseLineFunction& parse_line);

// Returns the reports that `parse_line` finds in `lines` (Python:
// BaseParser.get_reports_from_iter()).
Result<std::vector<Report>> ParseLineList(std::vector<std::string> lines,
                                          const ParseLineFunction& parse_line);

// Returns the number in `digits`, which a regular expression matched as
// \d+ (Python: int(match.group(...))).
int ToInt(std::string_view digits);

// Returns the path `path` from analyzer output relative to the folder of the
// input file `input`: os.path.join(dirname(input), path).
std::string RelativeToInput(const std::string& input, std::string_view path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_LINE_PARSER_H_
