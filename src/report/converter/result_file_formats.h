// Ports of the report-converter types whose input is already a result file
// format (report-converter.md §4.4, §4.5): cppcheck (plist) and gcc
// (SARIF).

#ifndef CC_REPORT_CONVERTER_RESULT_FILE_FORMATS_H_
#define CC_REPORT_CONVERTER_RESULT_FILE_FORMATS_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Returns the reports of a Cppcheck plist folder (every *.plist file directly
// in it) or of one .plist file. Each report gets a last event with its
// message at its location, unless it already ends with one.
Result<std::vector<Report>> GetCppcheckReports(const std::string& path);

// Returns the reports of a GCC -fanalyzer SARIF file.
Result<std::vector<Report>> GetGccReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_RESULT_FILE_FORMATS_H_
