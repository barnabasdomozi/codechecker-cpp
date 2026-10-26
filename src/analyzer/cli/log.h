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

// The environment variables of `log`, as its help lists them (Python:
// EPILOG_ENV_VAR of cli/log.py). The help of `check` lists them too.
inline constexpr std::string_view kLogEnvironmentVariables = R"(
  CC_LOGGER_ABS_PATH       If the environment variable is defined, all relative
                           paths in the compilation commands after '-I,
                           -idirafter, -imultilib, -iquote, -isysroot -isystem,
                           -iwithprefix, -iwithprefixbefore, -sysroot,
                           --sysroot' will be converted to absolute PATH when
                           written into the compilation database.
  CC_LOGGER_DEBUG_FILE     Output file to print log messages. By default if we
                           run the log command in debug mode it will generate
                           a 'codechecker.logger.debug' file beside the log
                           file.
  CC_LOGGER_DEF_DIRS       If the environment variable is defined, the logger
                           will extend the compiler argument list in the
                           compilation database with the pre-configured include
                           paths of the logged compiler.
  CC_LOGGER_GCC_LIKE       Set to to a colon separated list to change which
                           compilers should be logged. For example (default):
                           export CC_LOGGER_GCC_LIKE="gcc:g++:clang:clang++:
                           /cc:c++". The logger will match any compilers with
                           'gcc', 'g++', 'clang', 'clang++', 'cc' and 'c++' in
                           their filenames.
  CC_LOGGER_KEEP_LINK      If its value is not 'true' then object files will be
                           removed from the build action. For example in case
                           of this build command: 'gcc main.c object1.o
                           object2.so' the 'object1.o' and 'object2.so' will be
                           removed and only 'gcc main.c' will be captured. If
                           only object files are provided to the compiler then
                           the complete build action will be thrown away. This
                           means that build actions which only perform linking
                           will not be captured. We consider a file as object
                           file if its extension is '.o', '.so' or '.a'.
)";

// Adds the arguments of `log`. The description names the logger that will
// be used, so this looks for ldlogger.so, and for intercept-build if that
// is missing.
void AddLogArguments(util::argparse::Parser& parser);

// Runs `log` and returns the exit code: the build's exit code, or 1 if the
// build could not be run.
int RunLog(const util::argparse::Namespace& args);

}  // namespace cc::analyzer::cli

#endif  // CC_ANALYZER_CLI_LOG_H_
