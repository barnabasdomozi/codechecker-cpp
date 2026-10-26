// Port of codechecker_analyzer/cli/check.py: the `check` subcommand, which
// runs `log` (for a build command), `analyze` and `parse` one after the
// other (check.md).

#ifndef CC_ANALYZER_CLI_CHECK_H_
#define CC_ANALYZER_CLI_CHECK_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::analyzer::cli {

// The help of `check` in the subcommand list.
inline constexpr std::string_view kCheckHelp =
    "Perform analysis on a project and print results to standard output.";

// Adds the arguments of `check`. Like AddAnalyzeArguments(), this runs
// Clang for some defaults, and ends the program with exit code 1 if the
// analyzer context cannot be set up.
void AddCheckArguments(util::argparse::Parser& parser);

// Runs `check` and returns the exit code: the exit code of `parse` if it
// printed a report (2) or failed, else the analysis status of `analyze` (0,
// or 3 if an analysis failed). A failed build ends the command with the
// build's exit code; other errors end it with 1.
int RunCheck(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_CHECK_H_
