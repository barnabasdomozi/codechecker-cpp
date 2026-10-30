// Port of codechecker_analyzer/cli/checkers.py: the `checkers` subcommand,
// which lists the checkers of the analyzers with their status and labels,
// filtered by labels, or lists the profiles, severities, guidelines, labels
// and checker options (checkers.md).

#ifndef CC_ANALYZER_CLI_CHECKERS_H_
#define CC_ANALYZER_CLI_CHECKERS_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::analyzer::cli {

// The help of `checkers` in the subcommand list.
inline constexpr std::string_view kCheckersHelp =
    "List the checkers available for code analysis.";

// Adds the arguments of `checkers`.
void AddCheckersArguments(util::argparse::Parser& parser);

// Runs `checkers` and returns the exit code: 0, or 1 for a bad filter and
// for --checker-config with an analyzer that has no checker options.
int RunCheckers(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_CHECKERS_H_
