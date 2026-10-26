// Port of codechecker_report_converter/report/output/json.py and of
// dump_json_output() in codechecker_report_converter/util.py: the JSON
// export of `parse` (parse-export.md "JSON"), and how the JSON exports are
// written.

#ifndef CC_REPORT_OUTPUT_JSON_H_
#define CC_REPORT_OUTPUT_JSON_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Returns {"version": 1, "reports": [...]} with each report as
// ReportToJson() builds it, as json.dumps() writes it (Python: convert()).
std::string ReportsToJson(const std::vector<Report>& reports);

// Writes the JSON text `json` into the file at `output_path`, replacing it,
// and logs that it did; without a path, prints `json` and a newline on
// stdout (Python: dump_json_output()). Fails with the text of Python's
// OSError.
Result<void> DumpJsonOutput(std::string_view json,
                            const std::optional<std::string>& output_path);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_JSON_H_
