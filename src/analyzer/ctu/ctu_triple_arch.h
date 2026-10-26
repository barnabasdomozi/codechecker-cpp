// Port of codechecker_analyzer/analyzers/clangsa/ctu_triple_arch.py: the
// compile command that CTU and statistics use, and the target architecture
// of an action (ctu-analysis.md §3).

#ifndef CC_ANALYZER_CTU_CTU_TRIPLE_ARCH_H_
#define CC_ANALYZER_CTU_CTU_TRIPLE_ARCH_H_

#include <string>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/analyzer.h"

namespace cc::analyzer {

// Returns the Clang compile command of `action` (Python:
// get_compile_command()): with `output` (if not empty) as -o and `source`
// (if not empty) at the end, followed by the implicit standard.
std::vector<std::string> CtuCompileCommand(const BuildAction& action,
                                           const AnalyzerConfig& config,
                                           const std::string& source = "",
                                           const std::string& output = "");

// Returns the architecture part of the target triple of `action`, for
// example "x86_64", from Clang's -### output; "" if it is not found
// (Python: get_triple_arch()).
std::string TripleArch(const BuildAction& action, const std::string& source,
                       const AnalyzerConfig& config);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_CTU_CTU_TRIPLE_ARCH_H_
