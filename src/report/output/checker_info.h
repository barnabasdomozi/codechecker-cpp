// What the HTML and SARIF outputs show of a checker's labels. The labels
// themselves are read by cc_common, which depends on this library, so the
// caller gives the outputs a function that looks them up (Python: the
// CheckerLabels object these outputs are given).

#ifndef CC_REPORT_OUTPUT_CHECKER_INFO_H_
#define CC_REPORT_OUTPUT_CHECKER_INFO_H_

#include <functional>
#include <optional>
#include <string>

namespace cc::report {

// The labels of a checker that the outputs show.
struct CheckerInfo {
  std::string severity;  // The severity label, or its default.
  std::optional<std::string> doc_url;  // The first doc_url label.
};

// Returns the CheckerInfo of `checker` of the analyzer `analyzer`, or of any
// analyzer for nullopt. An empty function means the labels are not known.
using CheckerInfoLookup = std::function<CheckerInfo(
    const std::string& checker, const std::optional<std::string>& analyzer)>;

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_CHECKER_INFO_H_
