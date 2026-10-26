#include "analyzer/cli/check.h"

#include <stdlib.h>

#include <cstdio>
#include <exception>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "analyzer/cli/analyze.h"
#include "analyzer/cli/log.h"
#include "analyzer/cli/parse.h"
#include "common/cli/verbose.h"
#include "common/source_code_comment_handler.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::analyzer::cli {
namespace {

namespace argparse = util::argparse;

// The logger of `check`.
constexpr util::Logger kLog("system");

// The description at the top of the help of `check`.
constexpr std::string_view kDescription = R"(
Run analysis for a project with printing results immediately on the standard
output. Check only needs a build command or an already existing logfile and
performs every step of doing the analysis in batch.)";

// The {} are, in this order, the environment variables of `log`, `analyze`
// and `parse`, and the issue hashes.
constexpr std::string_view kEpilog = R"(
Environment variables
------------------------------------------------
Environment variables for 'codechecker-cpp log' command:
{}

Environment variables for 'codechecker-cpp analyze' command:
{}

Environment variables for 'codechecker-cpp parse' command:
{}

{}

Exit status
------------------------------------------------
0 - No report
1 - CodeChecker error
2 - At least one report emitted by an analyzer
3 - Analysis of at least one translation unit failed
128+signum - Terminating on a fatal signal whose number is signum

If you wish to reuse the logfile resulting from executing the build, see
'codechecker-cpp log'. To keep analysis results for later, see and use
'codechecker-cpp analyze'. To print human-readable output from previously saved
analysis results, see 'codechecker-cpp parse'. 'codechecker-cpp check' exposes a
wrapper calling these three commands in succession. Please make sure your build
command actually builds the files -- it is advised to execute builds on empty
trees, aka. after a 'make clean', as CodeChecker only analyzes files that had
been used by the build system.
)";

// The description of the "log arguments" help section.
constexpr std::string_view kLogDescription = R"(
Specify how the build information database should be obtained. You need to
specify either an already existing log file, or a build command which will be
used to generate a log file on the fly.)";

// The environment variable that names ldlogger's debug file.
constexpr char kDebugFileVariable[] = "CC_LOGGER_DEBUG_FILE";

// The options of `check` that `log` gets as they are.
constexpr std::string_view kLogOptions[] = {"quiet", "verbose",
                                            "use_absolute_ldpreload_path"};

// The options of `check` that `analyze` gets as they are (Python:
// args_to_update and the other options that check.main() passes on).
constexpr std::string_view kAnalyzeOptions[] = {
    "jobs", "keep_gcc_include_fixed", "keep_gcc_intrin", "quiet", "skipfile",
    "drop_skipped_reports", "files", "analyzers", "cppcheck_args_cfg_file",
    "clangsa_args_cfg_file", "tidy_args_cfg_file", "analyzer_config",
    "checker_config", "capture_analysis_output", "generate_reproducer",
    "config_file", "ctu_ast_mode", "ctu_phases", "ctu_reanalyze_on_failure",
    "stats_output", "stats_dir", "stats_enabled", "stats_relevance_threshold",
    "stats_min_sample_count", "enable_all", "disable_all",
    "no_missing_checker_error", "ordered_checkers", "timeout",
    "review_status_config", "compile_uniqueing", "report_hash",
    "add_gcc_include_dirs_with_isystem", "compiler_info_file",
    "dump_compiler_info_file", "enable_z3_refutation", "clean", "verbose"};

// The options of `check` that `parse` gets as they are.
constexpr std::string_view kParseOptions[] = {
    "print_steps", "trim_path_prefix", "review_status", "verbose", "skipfile",
    "suppress"};

// Sets the values of the `keys` that `from` has in `to` (Python:
// __update_if_key_exists()).
void CopyGiven(const argparse::Namespace& from,
               std::span<const std::string_view> keys,
               argparse::Namespace& to) {
  for (const std::string_view key : keys) {
    if (from.Has(key)) to.SetList(key, from.GetList(key));
  }
}

// Runs `log` for the build command of `args`, writing the build log to
// `logfile`. ldlogger's debug file goes to `output_dir` unless
// CC_LOGGER_DEBUG_FILE is set. Returns the error that ends the command if
// the build fails.
Result<void> LogBuild(const argparse::Namespace& args,
                      const std::string& logfile,
                      const std::string& output_dir) {
  argparse::Namespace log_args;
  log_args.Set("command", args.Get("command"));
  log_args.Set("logfile", logfile);
  CopyGiven(args, kLogOptions, log_args);
  // The debug file of ldlogger goes to the output folder, so that checks
  // running at the same time do not write the same file in the temporary
  // folder.
  if (!platform::GetEnv(kDebugFileVariable)) {
    setenv(kDebugFileVariable,
           util::pypath::Join({output_dir, "codechecker.logger.debug"}).c_str(),
           1);
  }
  if (const int exit_code = RunLog(log_args); exit_code != 0) {
    return Result<void>(Error{.exit_code = exit_code});
  }
  return Result<void>();
}

// Runs the steps of `check` with the results in `output_dir`: `log` if
// there is a build command, then `analyze` and `parse`. Returns the
// analysis status (0 or 3) if `parse` printed no report, or the error that
// ends the command (Python: sys.exit() in one of the steps). `temp_logfile`
// receives the build log that `log` writes, which the caller deletes.
Result<int> LogAnalyzeAndParse(const argparse::Namespace& args,
                               const std::string& output_dir,
                               std::optional<std::string>& temp_logfile) {
  std::string logfile;
  if (args.Has("command")) {
    temp_logfile = platform::MakeTempFile(platform::TempDir(), "");
    if (!temp_logfile) {
      // Python catches the exception and returns the initial status, 1.
      kLog.Error("Running check failed.");
      return Result<int>(Error{});
    }
    logfile = *temp_logfile;
    if (Result<void> logged = LogBuild(args, logfile, output_dir);
        !logged.ok()) {
      return Result<int>(logged.error());
    }
  } else {
    logfile = args.Get("logfile");
  }

  argparse::Namespace analyze_args;
  analyze_args.Set("input", logfile);
  analyze_args.Set("output_path", output_dir);
  analyze_args.Set("output_format", "plist");
  CopyGiven(args, kAnalyzeOptions, analyze_args);
  const Result<int> analysis_status = Analyze(analyze_args);
  if (!analysis_status.ok()) return analysis_status;

  argparse::Namespace parse_args;
  parse_args.SetList("input", {output_dir});
  parse_args.Set("input_format", "plist");
  CopyGiven(args, kParseOptions, parse_args);
  if (const int exit_code = RunParse(parse_args); exit_code != 0) {
    return Result<int>(Error{.exit_code = exit_code});
  }
  return analysis_status;
}

}  // namespace

void AddCheckArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));
  parser.set_epilog(std::format(kEpilog, kLogEnvironmentVariables,
                                kAnalyzeEnvironmentVariables,
                                kParseEnvironmentVariables, kIssueHashes));

  parser.AddArgument(
      {.flags = {"-o", "--output"},
       .dest = "output_dir",
       .help = "Store the analysis output in the given folder. If it is not "
               "given then the results go into a temporary directory which "
               "will be removed after the analysis."});
  AddOutputFormatArgument(parser);
  parser.AddArgument(
      {.flags = {"-q", "--quiet"},
       .dest = "quiet",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "If specified, the build tool's and the analyzers' output will "
               "not be printed to the standard output."});
  AddCompilerInfoArguments(parser);

  const int log_group =
      parser.AddArgumentGroup("log arguments", std::string(kLogDescription));
  const int log_input = parser.AddMutuallyExclusiveGroup(
      argparse::ExclusiveGroup::kRequired);
  parser.AddArgument(
      {.flags = {"-b", "--build"},
       .dest = "command",
       .help = "Execute and record a build command. Build commands can be "
               "simple calls to 'g++' or 'clang++' or 'make', but a more "
               "complex command, or the call of a custom script file is also "
               "supported.",
       .group = log_group,
       .exclusive_group = log_input});
  parser.AddArgument(
      {.flags = {"-l", "--logfile"},
       .dest = "logfile",
       .help = "Use an already existing JSON compilation command database "
               "file specified at this path.",
       .group = log_group,
       .exclusive_group = log_input});
  const int log_extra_group = parser.AddArgumentGroup(
      "log extra arguments",
      "Additional arguments which can be forwarded to the logging phase.");
  parser.AddArgument(
      {.flags = {"--use-absolute-ldpreload-path"},
       .dest = "use_absolute_ldpreload_path",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Use absolute paths in LD_PRELOAD environment variable instead "
               "of relying on LD_LIBRARY_PATH for loading the ldlogger.so "
               "library.",
       .group = log_extra_group});

  const int analyzer_group = parser.AddArgumentGroup("analyzer arguments", "");
  AddJobsArgument(parser, analyzer_group);
  AddCleanArgument(parser, analyzer_group);
  AddCompileUniqueingArgument(parser);
  AddReportHashArgument(parser, analyzer_group);
  AddFileFilterArguments(parser);
  AddAnalyzerArguments(parser, analyzer_group);
  AddReviewStatusConfigArgument(parser, analyzer_group);
  AddZ3RefutationArgument(parser, analyzer_group);
  AddCtuStatisticsAndCheckerArguments(parser);

  const int output_group = parser.AddArgumentGroup("output arguments", "");
  AddPrintStepsArgument(parser, output_group);
  AddSuppressArgument(parser, output_group);
  AddTrimPathPrefixArgument(parser, output_group);
  // Python's help text is not an f-string, so it shows the braces as they
  // are.
  parser.AddArgument(
      {.flags = {"--review-status"},
       .dest = "review_status",
       .nargs = argparse::Nargs::kZeroOrMore,
       .metavar = "REVIEW_STATUS",
       .choices = std::vector<std::string>(
           std::begin(common::kReviewStatusValues),
           std::end(common::kReviewStatusValues)),
       .default_list = std::vector<std::string>{"confirmed", "unreviewed"},
       .help = "Filter results by review statuses. Valid values are: "
               "{', '.join(REVIEW_STATUS_VALUES)}"});
  common::cli::AddVerboseArgument(parser);
}

int RunCheck(const argparse::Namespace& args) {
  util::SetupLogger(common::cli::Verbose(args), util::Stream::kStdout);
  if (args.Has("ctu_ast_mode") && !args.Has("ctu_phases")) {
    kLog.Error("Analyzer option 'ctu-ast-mode' requires CTU mode enabled");
    return 1;
  }

  // Without -o, the results go to a temporary folder, so that later checks
  // do not show them.
  std::string output_dir;
  if (args.Has("output_dir")) {
    output_dir = args.Get("output_dir");
  } else {
    const std::optional<std::string> temp_dir = platform::MakeTempDir();
    // Python: an exception that nothing catches.
    if (!temp_dir) std::terminate();
    output_dir = *temp_dir;
  }
  output_dir = util::pypath::Abspath(output_dir);
  if (!util::pypath::Exists(output_dir)) {
    if (const std::error_code error = platform::MakeDirs(output_dir)) {
      // Python ends with an uncaught exception here.
      util::Print(util::OsError(error.value(), output_dir).message);
      return 1;
    }
  }

  std::optional<std::string> temp_logfile;
  const Result<int> result =
      LogAnalyzeAndParse(args, output_dir, temp_logfile);
  if (!args.Has("output_dir")) platform::RemoveTree(output_dir);
  if (temp_logfile) std::remove(temp_logfile->c_str());
  if (!result.ok()) return result.error().exit_code;
  kLog.Debug("Check finished.");
  return result.value();
}

}  // namespace cc::analyzer::cli
