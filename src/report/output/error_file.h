// Port of codechecker_report_converter/report/error_file.py: the side file
// of a failed analyzer run (analyzer-drivers.md §6).

#ifndef CC_REPORT_OUTPUT_ERROR_FILE_H_
#define CC_REPORT_OUTPUT_ERROR_FILE_H_

#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::report {

// Deletes `<result_file>.err`, and if `return_code` is not 0, writes it as a
// plist dictionary with the analyzer's name, command (its arguments joined
// without separators, as Python joins the list), return code and output.
// Characters that are not printable are written as \xHH escapes.
Result<void> UpdateErrorFile(const std::string& result_file, int return_code,
                             std::string_view analyzer_name,
                             const std::vector<std::string>& analyzer_cmd,
                             std::string_view out, std::string_view err);

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_ERROR_FILE_H_
