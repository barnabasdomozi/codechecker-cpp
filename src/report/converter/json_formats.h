// Ports of the report-converter types that read JSON output
// (report-converter.md §4.6, §4.8): pylint, ruff, eslint, tslint,
// pvs-studio, jscpd, pmd and fbinfer.

#ifndef CC_REPORT_CONVERTER_JSON_FORMATS_H_
#define CC_REPORT_CONVERTER_JSON_FORMATS_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// These functions return the reports in the JSON file at `path`. A file
// that is not valid JSON is logged and has no reports, except for pmd and
// fbinfer, where it is an error as in the Python version. Malformed
// findings (missing keys) are errors, as in the Python version.
Result<std::vector<Report>> GetPylintReports(const std::string& path);
Result<std::vector<Report>> GetRuffReports(const std::string& path);
Result<std::vector<Report>> GetEslintReports(const std::string& path);
Result<std::vector<Report>> GetTslintReports(const std::string& path);
Result<std::vector<Report>> GetPvsStudioReports(const std::string& path);
Result<std::vector<Report>> GetJscpdReports(const std::string& path);
Result<std::vector<Report>> GetPmdReports(const std::string& path);
Result<std::vector<Report>> GetInferReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_JSON_FORMATS_H_
