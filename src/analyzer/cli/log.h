// Port of codechecker_analyzer/cli/log.py, buildlog/build_manager.py and
// buildlog/host_check.py: the `log` subcommand, which runs a build with
// ldlogger.so preloaded (log.md).

#ifndef CC_ANALYZER_CLI_LOG_H_
#define CC_ANALYZER_CLI_LOG_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::analyzer::cli {

// The help of `log` in the subcommand list.
inline constexpr std::string_view kLogHelp =
    "Run a build command and collect the executed compilation commands, "
    "storing them in a JSON file.";

// Adds the arguments of `log`. The description names the logger that will
// be used, so this looks for ldlogger.so, and for intercept-build if that
// is missing.
void AddLogArguments(util::argparse::Parser& parser);

// Runs `log` and returns the exit code: the build's exit code, or 1 if the
// build could not be run.
int RunLog(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_LOG_H_
