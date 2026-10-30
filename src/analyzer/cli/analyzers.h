// Port of codechecker_analyzer/cli/analyzers.py: the `analyzers`
// subcommand, which lists the analyzers with their binaries and versions,
// lists the --analyzer-config options of one analyzer, or dumps its full
// configuration (analyzers.md).

#ifndef CC_ANALYZER_CLI_ANALYZERS_H_
#define CC_ANALYZER_CLI_ANALYZERS_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::analyzer::cli {

// The help of `analyzers` in the subcommand list.
inline constexpr std::string_view kAnalyzersHelp =
    "List supported and available analyzers.";

// Adds the arguments of `analyzers`.
void AddAnalyzersArguments(util::argparse::Parser& parser);

// Runs `analyzers` and returns the exit code: 0, or 1 if the package
// configuration cannot be read or --dump-config fails.
int RunAnalyzers(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_ANALYZERS_H_
