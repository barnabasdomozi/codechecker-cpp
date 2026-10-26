// Port of codechecker_report_converter/report/output/gerrit.py: the Gerrit
// review export of `parse` (parse-export.md "Gerrit").

#ifndef CC_REPORT_OUTPUT_GERRIT_H_
#define CC_REPORT_OUTPUT_GERRIT_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Returns whether the environment variables CC_REPO_DIR and
// CC_CHANGED_FILES are set, and logs an error for each one that is not
// (Python: mandatory_env_var_is_set()).
bool GerritEnvironmentIsSet();

// Returns `reports` as a Gerrit review, as json.dumps() writes it (Python:
// convert()). Paths are made relative to CC_REPO_DIR; only the reports in
// the files listed in the CC_CHANGED_FILES file become comments, the others
// are listed in the message, which links CC_REPORT_URL. Fails if the
// changed files cannot be read.
Result<std::string> ReportsToGerrit(const std::vector<Report>& reports);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_GERRIT_H_
