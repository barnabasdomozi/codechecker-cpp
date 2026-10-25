// Port of codechecker_analyzer/cli/analyzer_version.py: the
// `analyzer-version` subcommand.

#ifndef CC_ANALYZER_CLI_ANALYZER_VERSION_H_
#define CC_ANALYZER_CLI_ANALYZER_VERSION_H_

#include <string_view>

#include "common/version.h"
#include "util/argparse.h"
#include "util/result.h"

namespace cc::analyzer::cli {

// The help of `analyzer-version` in the subcommand list.
inline constexpr std::string_view kAnalyzerVersionHelp =
    "Print the version of CodeChecker analyzer package that is being used.";

void AddAnalyzerVersionArguments(util::argparse::Parser& parser);

// Runs `analyzer-version` and returns the exit code.
int RunAnalyzerVersion(const util::argparse::Namespace& args);

// Reads the version of the analyzer part from config/analyzer_version.json
// in the package.
Result<common::VersionInfo> ReadAnalyzerVersion();

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_ANALYZER_VERSION_H_
