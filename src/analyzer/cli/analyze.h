// Port of codechecker_analyzer/cli/analyze.py and analyzer.py: the
// `analyze` subcommand (analyze.md).

#ifndef CC_ANALYZER_CLI_ANALYZE_H_
#define CC_ANALYZER_CLI_ANALYZE_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::analyzer::cli {

// The help of `analyze` in the subcommand list.
inline constexpr std::string_view kAnalyzeHelp =
    "Execute the supported code analyzers for the files recorded in a JSON "
    "Compilation Database.";

// Adds the arguments of `analyze`. The defaults of --z3-refutation and the
// statistics options depend on what Clang can do, so this runs Clang; if the
// analyzer context cannot be set up, the program ends with exit code 1, as
// in the Python version.
void AddAnalyzeArguments(util::argparse::Parser& parser);

// Runs `analyze` and returns the exit code.
int RunAnalyze(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_ANALYZE_H_
