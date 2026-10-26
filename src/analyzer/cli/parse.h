// Port of codechecker_analyzer/cli/parse.py: the `parse` subcommand, which
// prints the reports of analyzer result files with summary tables, exports
// them to other formats (parse-export.md), or prints the analysis status of
// a report folder (parse.md).

#ifndef CC_ANALYZER_CLI_PARSE_H_
#define CC_ANALYZER_CLI_PARSE_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::analyzer::cli {

// The help of `parse` in the subcommand list.
inline constexpr std::string_view kParseHelp =
    "Print analysis summary and results in a human-readable format.";

// Adds the arguments of `parse`.
void AddParseArguments(util::argparse::Parser& parser);

// Runs `parse` and returns the exit code: 2 if a report was printed or
// exported, 0 if none was (or for --status), 1 on errors.
int RunParse(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_PARSE_H_
