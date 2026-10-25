// Port of codechecker_report_converter/analyzers/sanitizers/: the output of
// Clang's sanitizers (report-converter.md §4.1).

#ifndef CC_REPORT_CONVERTER_SANITIZERS_H_
#define CC_REPORT_CONVERTER_SANITIZERS_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// These functions return the reports in the sanitizer output file at
// `path`. Stack frames give events only for source files that exist.
Result<std::vector<Report>> GetAsanReports(const std::string& path);
Result<std::vector<Report>> GetMsanReports(const std::string& path);
Result<std::vector<Report>> GetTsanReports(const std::string& path);
Result<std::vector<Report>> GetLsanReports(const std::string& path);
Result<std::vector<Report>> GetUbsanReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_SANITIZERS_H_
