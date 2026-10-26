#include "analyzer/cli/log.h"

#include <errno.h>

#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "common/cli/verbose.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("buildlogger");
// The logger of buildlog/host_check.py.
constexpr util::Logger kSystemLog("system");

constexpr char kDebugFileVariable[] = "CC_LOGGER_DEBUG_FILE";

constexpr std::string_view kDescriptionStart =
    "\nRuns the given build command and records the executed compilation "
    "steps. These\nsteps are written to the output file in a JSON format.\n"
    "\nAvailable build logger tool that will be used is\n'";

constexpr std::string_view kLdloggerSettings =
    "\nld-logger can be fine-tuned with some environment variables. For "
    "details see\nthe following documentation:\n"
    "https://github.com/Ericsson/codechecker/blob/master/analyzer/tools/\n"
    "build-logger/README.md#usage";

constexpr std::string_view kEpilog = R"(
Environment variables
------------------------------------------------

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

// Returns whether the package has ldlogger.so (Python:
// host_check.check_ldlogger()).
bool CheckLdlogger() {
  if (platform::GetEnv("CC_DATA_FILES_DIR").value_or("").empty() &&
      platform::GetEnv("CC_LIB_DIR").value_or("").empty()) {
    kSystemLog.Debug(
        "Both CC_DATA_FILES_DIR and CC_LIB_DIR environment variables as "
        "undefined or empty in the logger environment.");
    return false;
  }
  for (const std::string& root : LoggerLibRoots()) {
    for (const std::string& file : platform::WalkFiles(root)) {
      if (util::pypath::Basename(file) == kLoggerLibName) return true;
    }
  }
  return false;
}

// Returns whether `intercept-build --help` runs successfully (Python:
// host_check.check_intercept()).
bool CheckIntercept() {
  constexpr std::string_view kCommand = "intercept-build --help";
  const platform::RunResult result =
      platform::RunProcess({"intercept-build", "--help"},
                           {.stdout_mode = platform::Output::kDevNull,
                            .stderr_mode = platform::Output::kDevNull});
  if (result.start_error == ENOENT) {
    kSystemLog.Debug(
        util::OsError(result.start_error, result.start_error_path).message);
    kSystemLog.Debug(std::format("Failed to run: \"{}\"", kCommand));
  }
  if (result.start_error != 0) return false;
  if (result.return_code != 0) {
    kSystemLog.Debug(std::format(
        "Failed to run: \"{}\", process returned non-zero exit code.",
        kCommand));
    return false;
  }
  return true;
}

// Sets the variables of `env` that only `log` sets for ldlogger: the
// default compiler list, CC_LOGGER_KEEP_LINK, and the debug file, which is
// only kept or added with a debug `verbose` level.
void SetLoggerOptions(const std::string& logfile, bool keep_link,
                      std::optional<std::string_view> verbose,
                      std::vector<std::string>& env) {
  if (!platform::GetEnv(env, "CC_LOGGER_GCC_LIKE")) {
    platform::SetEnv(&env, "CC_LOGGER_GCC_LIKE",
                     "gcc:g++:clang:clang++:/cc:c++");
  }
  if (keep_link || platform::GetEnv(env, "CC_LOGGER_KEEP_LINK") == "true") {
    platform::SetEnv(&env, "CC_LOGGER_KEEP_LINK", "true");
  }

  const bool is_debug = verbose == "debug" || verbose == "debug_analyzer";
  const bool has_debug_file =
      platform::GetEnv(env, kDebugFileVariable).has_value();
  if (!is_debug && has_debug_file) {
    const std::string prefix = std::string(kDebugFileVariable) + "=";
    std::erase_if(env, [&prefix](const std::string& item) {
      return item.starts_with(prefix);
    });
  } else if (is_debug && !has_debug_file) {
    const std::string debug_file =
        platform::GetEnv(kDebugFileVariable)
            .value_or(util::pypath::Join({util::pypath::Dirname(logfile),
                                          "codechecker.logger.debug"}));
    if (util::pypath::Exists(debug_file)) std::remove(debug_file.c_str());
    platform::SetEnv(&env, kDebugFileVariable, debug_file);
  }
}

// Removes the complete lines from the start of `pending` and returns them.
// Lines end with "\n", "\r\n" or "\r", as in Python's universal newlines; a
// "\r" at the end stays, because a "\n" may still follow it.
std::string TakeLines(std::string& pending) {
  size_t end = pending.find_last_of("\r\n");
  if (end != std::string::npos && end + 1 == pending.size() &&
      pending[end] == '\r') {
    end = end == 0 ? std::string::npos : pending.find_last_of("\r\n", end - 1);
  }
  if (end == std::string::npos) return {};
  std::string lines = pending.substr(0, end + 1);
  pending.erase(0, end + 1);
  return lines;
}

// Writes the lines `text` of the build output to stdout, decoded the way
// Python reads them: UTF-8 with invalid bytes dropped, universal newlines.
void WriteBuildOutput(std::string_view text) {
  util::Write(util::Stream::kStdout,
              util::DecodeUtf8Ignore(util::TranslateNewlines(text)));
}

// Runs `command` with the shell in the environment `env`, with stderr merged
// into stdout, and echoes its output line by line unless `silent` (Python:
// execute_buildcmd()).
platform::RunResult ExecuteBuildCommand(const std::string& command,
                                        bool silent,
                                        const std::vector<std::string>& env) {
  std::string pending;
  const platform::RunOptions options = {
      .env = env,
      .stdout_mode = platform::Output::kPipe,
      .stderr_mode = platform::Output::kStdout,
      .on_stdout =
          [&pending, silent](std::string_view data) {
            if (silent) return;
            pending.append(data);
            WriteBuildOutput(TakeLines(pending));
          }};
  platform::RunResult result =
      platform::RunProcess({"/bin/sh", "-c", command}, options);
  WriteBuildOutput(pending);
  return result;
}

// Returns `env` as Python prints a dict of it.
std::string EnvRepr(const std::vector<std::string>& env) {
  std::vector<std::string> items;
  for (const std::string& item : env) {
    const size_t equals = item.find('=');
    items.push_back(util::Repr(item.substr(0, equals)) + ": " +
                    util::Repr(item.substr(equals + 1)));
  }
  return "{" + util::Join(items, ", ") + "}";
}

// Returns the environment to run the build in: the one saved by the
// launcher, or a copy of the current one if it cannot be read.
std::vector<std::string> BuildEnvironment() {
  Result<std::vector<std::string>> env = ReadOriginalEnvironment();
  if (env.ok()) return std::move(env).value();
  kLog.Warning(env.error().message);
  kLog.Warning(
      "Failed to get saved original_env using a current copy for logging.");
  return platform::CurrentEnvironment();
}

// Logs how the build `result` ended and returns the exit code of `log`:
// 0 on success, the build's exit code if it failed, 1 if it did not start.
int ReportBuildResult(const platform::RunResult& result,
                      const std::string& logfile) {
  if (result.start_error != 0) {
    kLog.Error("Calling original build command failed.");
    kLog.Error(
        util::OsError(result.start_error, result.start_error_path).message);
    return 1;
  }
  if (result.return_code != 0) {
    kLog.Info("Build failed.");
    return result.return_code;
  }
  kLog.Info("Build finished successfully.");
  kLog.Debug("The logfile is: " + logfile);
  return 0;
}

// Names the debug file of `env`, if any, and removes the lock files that
// ldlogger.so leaves next to it and next to `logfile`.
void CleanUpAfterBuild(const std::string& logfile,
                       const std::vector<std::string>& env) {
  const std::string debug_file =
      platform::GetEnv(env, kDebugFileVariable).value_or("");
  if (!debug_file.empty()) {
    kLog.Info("The debug log file is: " + debug_file);
    std::remove((debug_file + ".lock").c_str());
  }
  std::remove((logfile + ".lock").c_str());
}

// Runs the build `command` so that the compiler calls are written to
// `logfile`, and returns the exit code of `log` (Python:
// build_manager.perform_build_command()).
int PerformBuildCommand(const std::string& logfile, std::string command,
                        bool keep_link, bool silent,
                        std::optional<std::string_view> verbose,
                        bool use_absolute_ldpreload_path) {
  kLog.Info("Starting build...");
  std::vector<std::string> log_env = BuildEnvironment();
  if (CheckLdlogger()) {
    kLog.Info("Using CodeChecker ld-logger.");
    // Python creates the analyzer context here, which reports its errors.
    if (const Result<AnalyzerContext*> context = GetContext(); !context.ok()) {
      return context.error().exit_code;
    }
    SetLogEnvironment(logfile, use_absolute_ldpreload_path, log_env);
    SetLoggerOptions(logfile, keep_link, verbose, log_env);
  } else if (CheckIntercept()) {
    kLog.Info("Using intercept-build.");
    command = "intercept-build --cdb " + logfile + " sh -c " +
              util::ShlexQuote(command);
    kLog.DebugAnalyzer(command);
  } else {
    kLog.Error(
        "Both ldlogger and intercept-build are unavailable.\n"
        "Try acquiring the compilation_commands.json in another way.\n"
        "Install ldlogger or intercept-build to proceed.");
    return 1;
  }

  kLog.Debug(EnvRepr(log_env));
  const int exit_code =
      ReportBuildResult(ExecuteBuildCommand(command, silent, log_env), logfile);
  CleanUpAfterBuild(logfile, log_env);
  return exit_code;
}

}  // namespace

void AddLogArguments(argparse::Parser& parser) {
  // ldlogger is preferred over intercept-build.
  const bool is_ldlogger = CheckLdlogger();
  const bool is_intercept = !is_ldlogger && CheckIntercept();
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(
      std::string(kDescriptionStart) +
      (is_intercept ? "intercept-build" : "ld-logger") + "'." +
      std::string(is_ldlogger ? "" : kLdloggerSettings) + "\n");
  parser.set_epilog(std::string(kEpilog));

  parser.AddArgument(
      {.flags = {"-o", "--output"},
       .dest = "logfile",
       .required = true,
       .help = "Path of the file to write the collected compilation commands "
               "to. If the file already exists, it will be overwritten."});
  parser.AddArgument(
      {.flags = {"-b", "--build"},
       .dest = "command",
       .required = true,
       .help = "The build command to execute. Build commands can be simple "
               "calls to 'g++' or 'clang++' or 'make', but a more complex "
               "command, or the call of a custom script file is also "
               "supported."});
  parser.AddArgument(
      {.flags = {"-k", "--keep-link"},
       .dest = "keep_link",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "If this flag is given then the output log file will contain "
               "the linking build actions too."});
  parser.AddArgument(
      {.flags = {"-q", "--quiet"},
       .dest = "quiet",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Do not print the output of the build tool into the output of "
               "this command."});
  parser.AddArgument(
      {.flags = {"--use-absolute-ldpreload-path"},
       .dest = "use_absolute_ldpreload_path",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Use absolute paths in LD_PRELOAD environment variable instead "
               "of relying on LD_LIBRARY_PATH for loading the ldlogger.so "
               "library."});
  parser.AddArgument(
      {.flags = {"--verbose"},
       .dest = "verbose",
       .choices = {"info", "debug_analyzer", "debug"},
       .help = "Set verbosity level. If the value is 'debug' or "
               "'debug_analyzer' it will create a 'codechecker.logger.debug' "
               "debug log file beside the given output file. It will contain "
               "debug information of compilation database generation. You can "
               "override the location of this file if you set the "
               "'CC_LOGGER_DEBUG_FILE' environment variable to a different "
               "file path."});
}

int RunLog(const argparse::Namespace& args) {
  const std::optional<std::string_view> verbose = common::cli::Verbose(args);
  util::SetupLogger(verbose, util::Stream::kStdout);

  // The file is a valid, empty JSON array even if nothing gets logged.
  const std::string logfile = util::pypath::Realpath(args.Get("logfile"));
  if (const Result<void> written = util::WriteFile(logfile, "[\n]");
      !written.ok()) {
    // Python: an OSError that nothing catches; its text ends the traceback.
    util::Print(written.error().message);
    return 1;
  }

  return PerformBuildCommand(logfile, args.Get("command"),
                             args.Has("keep_link"), args.Has("quiet"),
                             verbose, args.Has("use_absolute_ldpreload_path"));
}

}  // namespace cc::analyzer::cli
