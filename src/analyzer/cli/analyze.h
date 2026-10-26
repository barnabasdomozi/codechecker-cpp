// Port of codechecker_analyzer/cli/analyze.py and analyzer.py: the
// `analyze` subcommand (analyze.md), and the arguments and help texts that
// `check` shares with it.

#ifndef CC_ANALYZER_CLI_ANALYZE_H_
#define CC_ANALYZER_CLI_ANALYZE_H_

#include <string_view>

#include "util/argparse.h"
#include "util/result.h"

namespace cc::analyzer::cli {

// The help of `analyze` in the subcommand list.
inline constexpr std::string_view kAnalyzeHelp =
    "Execute the supported code analyzers for the files recorded in a JSON "
    "Compilation Database.";

// The environment variables of `analyze`, as its help lists them (Python:
// EPILOG_ENV_VAR of cli/analyze.py). The help of `check` lists them too.
inline constexpr std::string_view kAnalyzeEnvironmentVariables = R"(
  CC_ANALYZERS_FROM_PATH   Set to `yes` or `1` to enforce taking the analyzers
                           from the `PATH` instead of the given binaries.
  CC_ANALYZER_BIN          Set the absolute paths of an analyzer binaries.
                           Overrides other means of CodeChecker getting hold of
                           binary.
                           Format: CC_ANALYZER_BIN='<analyzer1>:/path/to/bin1;
                                                    <analyzer2>:/path/to/bin2'
  CC_CLANGSA_PLUGIN_DIR    If the CC_ANALYZERS_FROM_PATH environment variable
                           is set you can configure the plugin directory of the
                           Clang Static Analyzer by using this environment
                           variable.
)";

// The "Issue hashes" part of the help of `analyze` and `check` (Python:
// EPILOG_ISSUE_HASHES of cli/analyze.py).
inline constexpr std::string_view kIssueHashes = R"(
Issue hashes
------------------------------------------------
- By default the issue hash calculation method for 'Clang Static Analyzer' is
context sensitive. It means the hash will be generated based on the following
information:
  * signature of the enclosing function declaration, type declaration or
    namespace.
  * content of the line where the bug is.
  * unique name of the checker.
  * position (column) within the line.

- By default the issue hash calculation method for 'Clang Tidy' is context
insensitive. It means the hash will be generated based on the following
information:
  * 'file name' from the main diag section.
  * 'checker name'.
  * 'checker message'.
  * 'line content' from the source file if can be read up.
  * 'column numbers' from the main diag section.
  * 'range column numbers' only from the control diag sections if column number
    in the range is not the same as the previous control diag section number in
    the bug path. If there are no control sections event section column numbers
    are used.

- context-free: there was a bug and for Clang Tidy the default hash was
generated and not the context free hash (kept for backward compatibility). Use
'context-free-v2' instead of this.

- context-free-v2:
  * 'file name' from the main diag section.
  * 'checker message'.
  * 'line content' from the source file if can be read up. All the whitespaces
    from the source content are removed.
  * 'column numbers' from the main diag sections location.

- diagnostic-message:
  * Same as 'context-free-v2' (file name, checker message etc.)
  * 'bug step messages' from all events.

  Be careful with this hash because it can change easily for example on
  variable / function renames.

OUR RECOMMENDATION: we recommend you to use 'context-free-v2' hash because the
hash will not be changed so easily for example on code indentation or when a
checker is renamed.

For more information see:
https://github.com/Ericsson/codechecker/blob/master/docs/analyzer/report_identification.md
)";

// The functions below add the arguments that `analyze` and `check` both
// have (Python defines them in both cli/analyze.py and cli/check.py). Where
// the two commands list an argument in different help sections, `group` is
// the section: an id from Parser::AddArgumentGroup(), or 0.

// Adds -j/--jobs.
void AddJobsArgument(util::argparse::Parser& parser, int group);

// Adds -c/--clean.
void AddCleanArgument(util::argparse::Parser& parser, int group);

// Adds -t/--type/--output-format.
void AddOutputFormatArgument(util::argparse::Parser& parser);

// Adds --compile-uniqueing.
void AddCompileUniqueingArgument(util::argparse::Parser& parser);

// Adds --report-hash.
void AddReportHashArgument(util::argparse::Parser& parser, int group);

// Adds --review-status-config.
void AddReviewStatusConfigArgument(util::argparse::Parser& parser, int group);

// Adds --z3-refutation. Its default depends on what Clang can do, so this
// runs Clang; if the analyzer context cannot be set up, the program ends
// with exit code 1, as in the Python version.
void AddZ3RefutationArgument(util::argparse::Parser& parser, int group);

// Adds --compiler-info-file, --dump-compiler-info-file,
// --keep-gcc-include-fixed, --keep-gcc-intrin and
// --add-gcc-include-dirs-with-isystem.
void AddCompilerInfoArguments(util::argparse::Parser& parser);

// Adds the "file filter arguments" section: -i/--ignore/--skip,
// --drop-reports-from-skipped-files and --file.
void AddFileFilterArguments(util::argparse::Parser& parser);

// Adds --analyzers, --capture-analysis-output, --generate-reproducer,
// --config, the deprecated --cppcheckargs, --saargs and --tidyargs,
// --analyzer-config, --checker-config and --timeout.
void AddAnalyzerArguments(util::argparse::Parser& parser, int group);

// Adds the sections of the cross translation unit, the statistics and the
// checker arguments. The statistics defaults depend on what Clang can do,
// so this runs Clang; if the analyzer context cannot be set up, the program
// ends with exit code 1, as in the Python version.
void AddCtuStatisticsAndCheckerArguments(util::argparse::Parser& parser);

// Adds the arguments of `analyze`. The defaults of --z3-refutation and the
// statistics options depend on what Clang can do, so this runs Clang; if the
// analyzer context cannot be set up, the program ends with exit code 1, as
// in the Python version.
void AddAnalyzeArguments(util::argparse::Parser& parser);

// Runs `analyze` (Python: analyze.main()) and returns the analysis status:
// 3 if the analysis of a translation unit failed, else 0. An error ends the
// command with its exit code, which is 0 if there was nothing to analyze or
// --dump-compiler-info-file was given; `check` then does not run `parse`.
Result<int> Analyze(const util::argparse::Namespace& args);

// Runs `analyze` and returns the exit code.
int RunAnalyze(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_ANALYZE_H_
