// Ports of the report-converter types that read XML output
// (report-converter.md §4.8): spotbugs and roslynator.

#ifndef CC_REPORT_CONVERTER_XML_FORMATS_H_
#define CC_REPORT_CONVERTER_XML_FORMATS_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// These functions return the reports in the XML file at `path`. A file that
// cannot be parsed is logged and has no reports.
Result<std::vector<Report>> GetSpotbugsReports(const std::string& path);
Result<std::vector<Report>> GetRoslynatorReports(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_XML_FORMATS_H_
