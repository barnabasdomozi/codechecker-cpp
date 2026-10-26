// Port of codechecker_report_converter/report/output/codeclimate.py: the
// Code Climate export of `parse`, which GitLab reads (parse-export.md
// "Code Climate / GitLab").

#ifndef CC_REPORT_OUTPUT_CODECLIMATE_H_
#define CC_REPORT_OUTPUT_CODECLIMATE_H_

#include <string>
#include <vector>

#include "report/model/report.h"

namespace cc::report {

// Returns `reports` as a list of Code Climate issues, as json.dumps()
// writes it (Python: convert()).
std::string ReportsToCodeClimate(const std::vector<Report>& reports);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_CODECLIMATE_H_
