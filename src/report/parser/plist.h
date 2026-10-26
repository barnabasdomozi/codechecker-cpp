// Reading plist result files (report-format.md §2; Python: get_reports() of
// codechecker_report_converter/report/parser/plist.py).

#ifndef CC_REPORT_PARSER_PLIST_H_
#define CC_REPORT_PARSER_PLIST_H_

#include <optional>
#include <string>
#include <vector>

#include "report/model/report.h"

namespace cc::report {

// Returns the reports of the plist file at `path`. Paths in the file are
// relative to `source_dir`, by default the folder of the file. Problems are
// logged as the Python version logs them; then the reports read so far are
// returned. Reports without a hash get the path-sensitive hash. Severities
// are not set (no checker labels).
std::vector<Report> ReadPlistFile(
    const std::string& path,
    const std::optional<std::string>& source_dir = std::nullopt);

}  // namespace cc::report

#endif  // CC_REPORT_PARSER_PLIST_H_
