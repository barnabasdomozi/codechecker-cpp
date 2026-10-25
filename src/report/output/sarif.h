// Writing SARIF result files (parse-export.md "SARIF"; Python: convert() and
// write() of codechecker_report_converter/report/parser/sarif.py).

#ifndef CC_REPORT_OUTPUT_SARIF_H_
#define CC_REPORT_OUTPUT_SARIF_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Writes `reports` into the SARIF file at `path`, replacing it, as
// json.dump() writes it. Checker labels are not known here, so rules have no
// helpUri and defaultConfiguration.
Result<void> WriteSarifFile(const std::string& path,
                            const std::vector<Report>& reports);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_SARIF_H_
