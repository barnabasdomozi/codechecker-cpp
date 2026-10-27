// Port of codechecker_report_converter/report/report_file.py: finding the
// analyzer result files of every supported format and reading them
// (report-format.md).

#ifndef CC_REPORT_PARSER_REPORT_FILE_H_
#define CC_REPORT_PARSER_REPORT_FILE_H_

#include <string>
#include <vector>

#include "report/model/report.h"

namespace cc::report {

// A folder and the analyzer result files in it.
struct ResultFolder {
  std::string path;
  std::vector<std::string> files;
};

// Returns whether `path` is an analyzer result file of a supported format,
// .plist or .sarif (Python: is_supported()).
bool IsSupported(const std::string& path);

// Returns the analyzer result files (.plist and .sarif) of `inputs`, by
// folder (Python: analyzer_result_files()). A file input is itself in its
// folder, or nothing if its format is not supported. A folder input gives
// each folder under it in os.walk() order, also the folders without result
// files. Inputs are made absolute first.
std::vector<ResultFolder> AnalyzerResultFiles(
    const std::vector<std::string>& inputs);

// Returns the reports of the result file at `path`, read by the parser of
// its format (Python: get_reports()); none for a format that is not
// supported. Severities are not set.
std::vector<Report> GetReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_PARSER_REPORT_FILE_H_
