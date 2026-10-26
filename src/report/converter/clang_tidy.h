// Port of codechecker_report_converter/analyzers/clang_tidy/ and
// clang_tidy_yaml/ (report-converter.md §4.2, §4.3). The text parser is
// also used by `analyze` for clang-tidy's output.

#ifndef CC_REPORT_CONVERTER_CLANG_TIDY_H_
#define CC_REPORT_CONVERTER_CLANG_TIDY_H_

#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Returns the reports in the clang-tidy console output file at `path`. As in
// the Python version, a line after a diagnostic that is not a fix-it, note
// or diagnostic line is an error ("Expected a FixIt line!").
Result<std::vector<Report>> GetClangTidyReports(const std::string& path);

// Returns the reports in `output`, clang-tidy's console output, split into
// lines like Python's str.splitlines() (the clang-tidy result handler of
// `analyze`).
Result<std::vector<Report>> ParseClangTidyOutput(std::string_view output);

// Returns the reports in the clang-tidy -export-fixes YAML file at `path`.
// Offsets are counted in characters of the source files. A file that cannot
// be read or an offset past its end is an error, as in the Python version.
Result<std::vector<Report>> GetClangTidyYamlReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_CLANG_TIDY_H_
