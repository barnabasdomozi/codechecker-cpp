// Port of codechecker_report_converter/report/output/baseline.py: baseline
// files, the sorted report hashes that `parse -e baseline` writes
// (parse-export.md "Baseline").

#ifndef CC_REPORT_OUTPUT_BASELINE_H_
#define CC_REPORT_OUTPUT_BASELINE_H_

#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// Returns whether `path` is the path of a baseline file: it ends with
// ".baseline" (Python: check()).
bool IsBaselineFile(std::string_view path);

// Returns the report hashes of `reports`, sorted and without repetitions
// (Python: convert()).
std::vector<std::string> BaselineHashes(const std::vector<Report>& reports);

// Adds `report_hashes` to the baseline file at `path`, which is created if
// it does not exist, and logs what it did (Python: write()). The file is
// rewritten only if a hash is new: with all hashes sorted, without
// repetitions, one per line, without a line end after the last one. Fails
// with the text of Python's OSError.
Result<void> WriteBaseline(const std::string& path,
                           const std::vector<std::string>& report_hashes);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_BASELINE_H_
