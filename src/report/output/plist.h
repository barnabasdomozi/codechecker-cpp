// Writing plist result files (report-format.md §2; Python: convert() and
// write() of codechecker_report_converter/report/parser/plist.py).

#ifndef CC_REPORT_OUTPUT_PLIST_H_
#define CC_REPORT_OUTPUT_PLIST_H_

#include <optional>
#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Writes `reports` into the plist file at `path`, replacing it.
// `analyzer_name`, if given, goes into the metadata. Fails like plistlib on
// strings with control characters, or if the file cannot be written.
Result<void> WritePlistFile(const std::string& path,
                            const std::vector<Report>& reports,
                            const std::optional<std::string>& analyzer_name);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_PLIST_H_
