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

// The environment variables of `parse`, as its help lists them (Python:
// EPILOG_ENV_VAR of cli/parse.py). The help of `check` lists them too.
inline constexpr std::string_view kParseEnvironmentVariables = R"(
  CC_CHANGED_FILES       Path of changed files json from Gerrit. Use it when
                         generating gerrit output.
  CC_REPO_DIR            Root directory of the sources, i.e. the directory
                         where the repository was cloned. Use it when
                         generating gerrit output.
  CC_REPORT_URL          URL where the report can be found. Use it when
                         generating gerrit output.
)";

// The functions below add the arguments that `parse` and `check` both have
// (Python defines them in both cli/parse.py and cli/check.py). `group` is
// the help section: an id from Parser::AddArgumentGroup(), or 0.

// Adds --suppress.
void AddSuppressArgument(util::argparse::Parser& parser, int group);

// Adds --print-steps.
void AddPrintStepsArgument(util::argparse::Parser& parser, int group);

// Adds --trim-path-prefix.
void AddTrimPathPrefixArgument(util::argparse::Parser& parser, int group);

// Adds the arguments of `parse`.
void AddParseArguments(util::argparse::Parser& parser);

// Runs `parse` and returns the exit code: 2 if a report was printed or
// exported, 0 if none was (or for --status), 1 on errors.
int RunParse(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_PARSE_H_
