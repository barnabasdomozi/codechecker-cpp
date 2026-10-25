// Reading SARIF result files (parse-export.md "Reading SARIF"; Python:
// get_reports() of codechecker_report_converter/report/parser/sarif.py).

#ifndef CC_REPORT_PARSER_SARIF_H_
#define CC_REPORT_PARSER_SARIF_H_

#include <string>
#include <vector>

#include "report/model/report.h"

namespace cc::report {

// Returns the reports of the SARIF file at `path`: one report per physical
// location of each result, with the code flow locations that have a message
// as events, and the path-sensitive hash. Problems are logged as the Python
// version logs them. A file with no runs, or with a uriBaseId that cannot be
// resolved, has no reports.
std::vector<Report> ReadSarifFile(const std::string& path);

}  // namespace cc::report

#endif  // CC_REPORT_PARSER_SARIF_H_
