// The program named in written result files (Python: get_tool_info() of
// codechecker_report_converter/report/parser/base.py).

#ifndef CC_REPORT_OUTPUT_TOOL_INFO_H_
#define CC_REPORT_OUTPUT_TOOL_INFO_H_

#include <string>

namespace cc::report {

struct ToolInfo {
  std::string name;
  std::string version;
};

// Returns CodeChecker and its version when the program runs inside
// CodeChecker (CC_DATA_FILES_DIR is set and has config/analyzer_version.json),
// else report-converter and its own version, 0.1.0.
ToolInfo GetToolInfo();

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_TOOL_INFO_H_
