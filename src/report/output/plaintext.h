// Port of codechecker_report_converter/report/output/plaintext.py: reports
// printed as text, as `parse` prints them (parse.md §4.2).

#ifndef CC_REPORT_OUTPUT_PLAINTEXT_H_
#define CC_REPORT_OUTPUT_PLAINTEXT_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "report/model/report.h"

namespace cc::report {

// The reports in one file. The reports are pointers into the vector they
// were grouped from.
struct FileReports {
  std::string path;  // Trimmed.
  std::vector<const Report*> reports;
};

// Reports by the path of their file, in the order the paths were first
// seen.
using FileReportMap = std::vector<FileReports>;

// Returns `reports` grouped by the path of their file (Python:
// get_file_report_map()). `source_file`, the source file the result file
// was made from, is added without reports if no report is in it.
FileReportMap GetFileReportMap(const std::vector<Report>& reports,
                               const std::optional<std::string>& source_file);

// Prints the reports of `file_report_map` on stdout, a file after the other
// in case-insensitive order of their paths, with their bug paths if
// `print_steps` (Python: convert()). A file without reports is printed as
// "Found no defects in <name>" only if it is not in `processed_file_paths`;
// the printed paths are added to it.
void PrintReports(const FileReportMap& file_report_map, bool print_steps,
                  std::set<std::string>* processed_file_paths);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_PLAINTEXT_H_
