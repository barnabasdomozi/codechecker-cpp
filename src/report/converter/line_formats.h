// Ports of the report-converter types that read one finding per line of
// text output (report-converter.md §4.7): cpplint, pyflakes, golint,
// markdownlint (mdl), sphinx, kernel-doc, smatch, coccinelle and sparse.

#ifndef CC_REPORT_CONVERTER_LINE_FORMATS_H_
#define CC_REPORT_CONVERTER_LINE_FORMATS_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// These functions return the reports in the tool output file at `path`.
// Paths in the output are relative to the folder of `path`; the source files
// do not have to exist.
Result<std::vector<Report>> GetCpplintReports(const std::string& path);
Result<std::vector<Report>> GetPyflakesReports(const std::string& path);
Result<std::vector<Report>> GetGolintReports(const std::string& path);
Result<std::vector<Report>> GetMarkdownlintReports(const std::string& path);
Result<std::vector<Report>> GetSphinxReports(const std::string& path);
Result<std::vector<Report>> GetKernelDocReports(const std::string& path);
Result<std::vector<Report>> GetSmatchReports(const std::string& path);
Result<std::vector<Report>> GetCoccinelleReports(const std::string& path);
Result<std::vector<Report>> GetSparseReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_LINE_FORMATS_H_
