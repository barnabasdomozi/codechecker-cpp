// The analyzer output types that report-converter reads (Python: the
// AnalyzerResult classes of codechecker_report_converter/analyzers/, found
// by cli.py).

#ifndef CC_REPORT_CONVERTER_ANALYZER_TYPES_H_
#define CC_REPORT_CONVERTER_ANALYZER_TYPES_H_

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "util/result.h"

namespace cc::report {

// One analyzer output type.
struct AnalyzerType {
  std::string_view tool_name;        // The -t value, for example "asan".
  std::string_view name;             // The tool's full name.
  std::string_view url;              // The tool's web page.
  std::string_view example_command;  // Printed by --example; may be empty.
  HashType hash_type;                // The report hash the type uses.
  // Returns the reports of one input file.
  Result<std::vector<Report>> (*get_reports)(const std::string& path);
};

// Returns the types in the order of cli.py's supported_converters (sorted
// by the paths of their Python modules).
std::span<const AnalyzerType> AnalyzerTypes();

// Returns the type named `tool_name`, or null.
const AnalyzerType* FindAnalyzerType(std::string_view tool_name);

}  // namespace cc::report

#endif  // CC_REPORT_CONVERTER_ANALYZER_TYPES_H_
