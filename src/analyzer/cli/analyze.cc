#include "analyzer/cli/analyze.h"

#include <yyjson.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "analyzer/analysis/analysis_manager.h"
#include "analyzer/analysis/makefile.h"
#include "analyzer/analysis/pre_analysis_manager.h"
#include "analyzer/buildlog/build_action.h"
#include "analyzer/buildlog/compilation_database.h"
#include "analyzer/buildlog/log_parser.h"
#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/analyzer_types.h"
#include "analyzer/drivers/clangsa.h"
#include "analyzer/metadata/metadata.h"
#include "common/checker_labels.h"
#include "common/cli/verbose.h"
#include "common/cmd_config.h"
#include "common/package.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "tu_collector/tu_collector.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");
constexpr util::Logger kAnalyzerLog("analyzer");

constexpr std::string_view kDescription =
    "\nUse the previously created JSON Compilation Database to perform an "
    "analysis on\nthe project, outputting analysis results in a "
    "machine-readable format.";

// The end of the help, after the issue hashes.
constexpr std::string_view kEpilogExitStatus = R"(


Exit status
------------------------------------------------
0 - Successful analysis
1 - CodeChecker error
3 - Analysis of at least one translation unit failed
128+signum - Terminating on a fatal signal whose number is signum


Compilation databases can be created by instrumenting your project's build via
'codechecker-cpp log'. To transform the results of the analysis to a human-friendly
format, please see the commands 'codechecker-cpp parse' or 'codechecker-cpp store'.
)";

constexpr std::string_view kCtuDescription = R"(
These arguments are only available if the Clang Static Analyzer supports
Cross-TU analysis. By default, no CTU analysis is run when
'codechecker-cpp analyze' is called.)";

constexpr std::string_view kStatsDescription = R"(
These arguments are only available if the Clang Static Analyzer supports
Statistics-based analysis (e.g. statisticsCollector.ReturnValueCheck,
statisticsCollector.SpecialReturnValue checkers are available).)";

constexpr std::string_view kCheckersDescription = R"(
Checkers
------------------------------------------------
An analyzer checks the source code with the help of checkers. Checkers
implement a specific rule, such as "don't divide by zero", and emit a warning
if the corresponding rule is violated. Available checkers can be listed by
'codechecker-cpp checkers'.

Checkers are grouped by CodeChecker via labels (described below), and sometimes
by their analyzer tool. An example for the latter is 'clangsa', which orders
checkers in a package hierarchy (e.g. in 'core.uninitialized.Assign', 'core'
and 'core.uninitialized' are packages).

Compiler warnings and errors
------------------------------------------------
Compiler warnings are diagnostic messages that report constructions that are
not inherently erroneous but that are risky or suggest there may have been an
error. However, CodeChecker views them as regular checkers.

Compiler warning names are transformed by CodeChecker to reflect the analyzer
name. For example, '-Wliteral-conversion' from clang-tidy is transformed to
'clang-diagnostic-literal-conversion'. However, they need to be enabled by
their original name, e.g. '-e Wliteral-conversion'.

Sometimes GCC is more permissive than Clang, so it is possible that a specific
construction doesn't compile with Clang but compiles with GCC. These
compiler errors are also collected as CodeChecker reports as
'clang-diagnostic-error'.
Note that compiler errors and warnings are captured by CodeChecker only if it
was emitted by clang-tidy.

Checker prefix groups
------------------------------------------------
Checker prefix groups allow you to enable checkers that share a common
prefix in their names. Checkers within a prefix group will have names that
start with the same identifier, making it easier to manage and reference
related checkers.

You can enable/disable checkers belonging to a checker prefix group:
'-e <label>:<value>', e.g. '-e prefix:security'.

Note: The 'prefix' label is mandatory when there is ambiguity between the
name of a checker prefix group and a checker profile or a guideline. This
prevents conflicts and ensures the correct checkers are applied.

See "codechecker-cpp checkers --help" to learn more.

Checker labels
------------------------------------------------
Each checker is assigned several '<label>:<value>' pairs. For instance,
'cppcheck-deallocret' has the labels 'profile:default' and 'severity:HIGH'. The
goal of labels is that you can enable or disable a batch of checkers with them.

You can enable/disable checkers belonging to a label: '-e <label>:<value>',
e.g. '-e profile:default'.

Note: The 'profile' label is mandatory when there is ambiguity between the
name of a checker profile and a checker prefix group or a guideline. This
prevents conflicts and ensures the correct checkers are applied.

See "codechecker-cpp checkers --help" to learn more.

Guidelines
------------------------------------------------
CodeChecker recognizes several third party coding guidelines, such as
CppCoreGuidelines, SEI-CERT, or MISRA. These are collections of best
programming practices to avoid common programming errors. Some checkers cover
the rules of these guidelines. CodeChecker assigns the 'guideline' label to
these checkers, such as 'guideline:sei-cert-c'. This way you can list and
enable those checkers which check the fulfillment of certain guideline rules.
See the output of "codechecker-cpp checkers --guideline" command.

Guidelines are labels themselves, and can be used as a label:
'-e guideline:<value>', e.g. '-e guideline:sei-cert-c'.

Note: The 'guideline' label is mandatory when there is ambiguity between the
name of a guideline and a checker prefix group or a checker profile. This
prevents conflicts and ensures the correct checkers are applied.

Batch enabling/disabling checkers
------------------------------------------------
You can fine-tune which checkers to use in the analysis by setting the enable
and disable flags starting from the bigger groups and going inwards. Taking
for example the package hierarchy of 'clangsa', '-e core -d core.uninitialized
-e core.uninitialized.Assign' will enable every 'core' checker, but only
'core.uninitialized.Assign' from the 'core.uninitialized' group. Mind that
disabling certain checkers - such as the 'core' group is unsupported by the
LLVM/Clang community, and thus discouraged.
)";

constexpr std::string_view kCompileUniqueingHelp =
    "Specify the method the compilation actions in the  compilation database "
    "are uniqued before analysis. CTU analysis works properly only if there "
    "is exactly one compilation action per source file. none(default in non "
    "CTU mode): no uniqueing is done. strict: no uniqueing is done, and an "
    "error is given if there is more than one compilation action for a "
    "source file. symlink: recognizes symlinks and removes duplication in the "
    "compilation database to ensure that each source file is analyzed only "
    "once. alpha(default in CTU mode): If there is more than one compilation "
    "action for a source file, only the one is kept that belongs to the "
    "alphabetically first compilation target. If none of the above given, "
    "this parameter should be a python regular expression. If there is more "
    "than one compilation action for a source, only the one is kept which "
    "matches the given python regex. If more than one matches an error is "
    "given. The whole compilation action text is searched for match.";

constexpr std::string_view kReportHashHelp =
    "R|Specify the hash calculation method for reports. By default the "
    "calculation method for Clang Static Analyzer is context sensitive and "
    "for Clang Tidy it is context insensitive.\nYou can use the following "
    "calculation methods:\n- context-free: there was a bug and for Clang "
    "Tidy not the context free hash was generated (kept for backward "
    "compatibility).\n- context-free-v2: context free hash is used for "
    "ClangSA and Clang Tidy.\n- diagnostic-message: context free hash with "
    "bug step messages is used for ClangSA and Clang Tidy.\nSee the 'issue "
    "hashes' section of the help message of this command below for more "
    "information.\nUSE WISELY AND AT YOUR OWN RISK!";

constexpr std::string_view kEnableHelpTail =
    " in the analysis. Labels can also be used for selecting checkers, for "
    "example profile:extreme or severity:STYLE. See 'codechecker-cpp checkers "
    "--label' for further details. In case of a name clash between the "
    "checker prefix group/profile/guideline name, the use of one of the "
    "following labels is mandatory: 'checker:', 'prefix:', 'profile:', "
    "'guideline:'. If a checker name matches multiple checkers as a prefix, "
    "'checker:' or 'prefix:' namespace is required";

// The CTU phases to run (Python: ctu_phases): both for --ctu, only
// `collect` for --ctu-collect, only `analyze` for --ctu-analyze.
struct CtuPhases {
  bool collect = false;
  bool analyze = false;
};

// The parsed options of `analyze` (Python: the attributes of `args`).
// std::optional and bools that are false mean "not in args".
struct AnalyzeArgs {
  std::string input;
  std::string output_path;
  int jobs = 1;
  std::optional<std::string> skipfile;
  bool drop_skipped_reports = false;
  std::optional<std::vector<std::string>> files;
  std::optional<std::string> review_status_config;
  std::optional<std::string> compiler_info_file;
  bool dump_compiler_info_file = false;
  bool keep_gcc_include_fixed = false;
  bool keep_gcc_intrin = false;
  bool add_gcc_include_dirs_with_isystem = false;
  bool makefile = false;
  bool quiet = false;
  bool clean = false;
  std::string compile_uniqueing;
  std::optional<std::string> report_hash;
  std::optional<std::string> name;
  std::optional<std::vector<std::string>> analyzers;
  bool capture_analysis_output = false;
  bool generate_reproducer = false;
  std::optional<std::string> config_file;
  std::vector<AnalyzerConfigArg> analyzer_config;
  std::optional<std::vector<CheckerConfigArg>> checker_config;
  std::optional<int> timeout;
  std::string enable_z3_refutation;
  std::optional<CtuPhases> ctu_phases;
  bool ctu_reanalyze_on_failure = false;
  std::optional<std::string> ctu_ast_mode;
  std::optional<std::string> stats_output;
  std::optional<std::string> stats_dir;
  bool stats_enabled = false;
  std::optional<int> stats_min_sample_count;
  std::optional<double> stats_relevance_threshold;
  std::optional<std::vector<CheckerChoice>> ordered_checkers;
  bool enable_all = false;
  bool disable_all = false;
  bool no_missing_checker_error = false;
};

// Python: existing_abspath().
Result<std::string> ExistingAbspath(const std::string& value) {
  const std::string path = util::pypath::Abspath(value);
  if (!util::pypath::Exists(path)) {
    return Result<std::string>(
        Error{.message = "File doesn't exist: " + path});
  }
  return Result<std::string>(path);
}

const util::Regex& AnalyzerConfigRegex() {
  static const util::Regex& regex =
      *new util::Regex("(?P<analyzer>.+):(?P<option>.+)=(?P<value>.+)");
  return regex;
}

const util::Regex& CheckerConfigRegex() {
  static const util::Regex& regex = *new util::Regex(
      "(?P<analyzer>.+):(?P<checker>.+):(?P<option>.+)=(?P<value>.+)");
  return regex;
}

// Python: analyzer_config() and checker_config() as argparse types.
Result<std::string> AnalyzerConfigType(const std::string& value) {
  if (!AnalyzerConfigRegex().Match(value)) {
    return Result<std::string>(Error{
        .message = "Analyzer option in wrong format: " + value +
                   ", should be <analyzer>:<option>=<value>"});
  }
  return Result<std::string>(value);
}

Result<std::string> CheckerConfigType(const std::string& value) {
  if (!CheckerConfigRegex().Match(value)) {
    return Result<std::string>(Error{
        .message = "Checker option in wrong format: " + value +
                   ", should be <analyzer>:<checker>:<option>=<value>"});
  }
  return Result<std::string>(value);
}

AnalyzerConfigArg ParseAnalyzerConfig(std::string_view text) {
  const util::RegexMatch match = *AnalyzerConfigRegex().Match(text);
  return {.analyzer = std::string(*match.Group("analyzer")),
          .option = std::string(*match.Group("option")),
          .value = std::string(*match.Group("value"))};
}

CheckerConfigArg ParseCheckerConfig(std::string_view text) {
  const util::RegexMatch match = *CheckerConfigRegex().Match(text);
  return {.analyzer = std::string(*match.Group("analyzer")),
          .checker = std::string(*match.Group("checker")),
          .option = std::string(*match.Group("option")),
          .value = std::string(*match.Group("value"))};
}

// Python: OrderedConfigAction: an item given again moves to the end.
Result<std::vector<std::string>> OrderedConfigAction(
    const std::vector<std::string>& values,
    const std::vector<std::string>* previous) {
  std::vector<std::string> result =
      previous ? *previous : std::vector<std::string>();
  for (const std::string& value : values) {
    std::erase(result, value);
    result.push_back(value);
  }
  return Result<std::vector<std::string>>(std::move(result));
}

// Python: OrderedCheckersAction for -e (`marker` "1") and -d ("0"): one list
// of the items in command line order.
argparse::CustomAction OrderedCheckersAction(std::string marker) {
  return [marker](const std::vector<std::string>& values,
                  const std::vector<std::string>* previous) {
    std::vector<std::string> result =
        previous ? *previous : std::vector<std::string>();
    result.push_back(marker + values.front());
    return Result<std::vector<std::string>>(std::move(result));
  };
}

// Returns the typed form of the parsed command line.
AnalyzeArgs ToAnalyzeArgs(const argparse::Namespace& ns) {
  const auto optional = [&ns](std::string_view dest) {
    return ns.Has(dest) ? std::optional<std::string>(ns.Get(dest))
                        : std::nullopt;
  };
  const auto list = [&ns](std::string_view dest) {
    return ns.Has(dest)
               ? std::optional<std::vector<std::string>>(ns.GetList(dest))
               : std::nullopt;
  };
  const auto flag = [&ns](std::string_view dest) {
    return ns.Has(dest) && ns.Get(dest) == "True";
  };
  AnalyzeArgs args;
  args.input = ns.Get("input");
  args.output_path = ns.Get("output_path");
  args.jobs = static_cast<int>(util::ParseInt(ns.Get("jobs")).value_or(1));
  args.skipfile = optional("skipfile");
  args.drop_skipped_reports = flag("drop_skipped_reports");
  args.files = list("files");
  args.review_status_config = optional("review_status_config");
  args.compiler_info_file = optional("compiler_info_file");
  args.dump_compiler_info_file = flag("dump_compiler_info_file");
  args.keep_gcc_include_fixed = flag("keep_gcc_include_fixed");
  args.keep_gcc_intrin = flag("keep_gcc_intrin");
  args.add_gcc_include_dirs_with_isystem =
      flag("add_gcc_include_dirs_with_isystem");
  args.makefile = flag("makefile");
  args.quiet = flag("quiet");
  args.clean = flag("clean");
  args.compile_uniqueing = ns.Get("compile_uniqueing");
  args.report_hash = optional("report_hash");
  args.name = optional("name");
  args.analyzers = list("analyzers");
  args.capture_analysis_output = flag("capture_analysis_output");
  args.generate_reproducer = flag("generate_reproducer");
  args.config_file = optional("config_file");
  for (const std::string& item : ns.GetList("analyzer_config")) {
    args.analyzer_config.push_back(ParseAnalyzerConfig(item));
  }
  if (ns.Has("checker_config")) {
    args.checker_config.emplace();
    for (const std::string& item : ns.GetList("checker_config")) {
      args.checker_config->push_back(ParseCheckerConfig(item));
    }
  }
  // Deprecated options become --analyzer-config items.
  for (const auto& [dest, analyzer, option] :
       {std::tuple("clangsa_args_cfg_file", "clangsa", "--saargs"),
        std::tuple("tidy_args_cfg_file", "clang-tidy", "--tidyargs"),
        std::tuple("cppcheck_args_cfg_file", "cppcheck", "--cppcheckargs")}) {
    if (!ns.Has(dest)) continue;
    args.analyzer_config.push_back(
        {.analyzer = analyzer,
         .option = "cc-verbatim-args-file",
         .value = ns.Get(dest)});
    kLog.Warning(std::format("\"{}\" is deprecated. Use \"--analyzer-config "
                             "{}:cc-verbatim-args-file=<filepath>\" instead.",
                             option, analyzer));
  }
  if (ns.Has("timeout")) {
    args.timeout =
        static_cast<int>(util::ParseInt(ns.Get("timeout")).value_or(0));
  }
  args.enable_z3_refutation = ns.Get("enable_z3_refutation");
  if (ns.Has("ctu_phases")) {
    const std::string& phases = ns.Get("ctu_phases");
    args.ctu_phases = CtuPhases{.collect = phases[0] == '1',
                                .analyze = phases[1] == '1'};
  }
  args.ctu_reanalyze_on_failure = flag("ctu_reanalyze_on_failure");
  args.ctu_ast_mode = optional("ctu_ast_mode");
  args.stats_output = optional("stats_output");
  args.stats_dir = optional("stats_dir");
  args.stats_enabled = flag("stats_enabled");
  if (ns.Has("stats_min_sample_count")) {
    args.stats_min_sample_count = static_cast<int>(
        util::ParseInt(ns.Get("stats_min_sample_count")).value_or(0));
  }
  if (ns.Has("stats_relevance_threshold")) {
    args.stats_relevance_threshold =
        std::strtod(ns.Get("stats_relevance_threshold").c_str(), nullptr);
  }
  if (ns.Has("ordered_checkers")) {
    args.ordered_checkers.emplace();
    for (const std::string& item : ns.GetList("ordered_checkers")) {
      args.ordered_checkers->push_back(
          {.name = item.substr(1), .enabled = item[0] == '1'});
    }
  }
  args.enable_all = flag("enable_all");
  args.disable_all = flag("disable_all");
  args.no_missing_checker_error = flag("no_missing_checker_error");
  return args;
}

// Python: is_analyzer_config_valid().
bool IsAnalyzerConfigValid(const AnalyzeArgs& args) {
  std::vector<std::string> messages;
  std::vector<std::string> available;
  for (const AnalyzerDriver* analyzer : SupportedAnalyzers()) {
    if (analyzer->Version()) available.emplace_back(analyzer->name());
  }
  std::vector<std::string> enabled;
  if (args.analyzers) {
    for (const std::string& name : available) {
      if (std::ranges::find(*args.analyzers, name) != args.analyzers->end()) {
        enabled.push_back(name);
      }
    }
  }
  if (!enabled.empty()) available = enabled;
  std::map<std::string, std::vector<AnalyzerOption>> options;
  for (const std::string& name : available) {
    options[name] = FindAnalyzer(name)->AnalyzerConfigOptions();
  }
  for (const AnalyzerConfigArg& item : args.analyzer_config) {
    if (!enabled.empty() &&
        std::ranges::find(enabled, item.analyzer) == enabled.end()) {
      messages.push_back(std::format(
          "The analyzer '{}' is not enabled. Enable it first using "
          "--analyzers to set the config '{}'.",
          item.analyzer, item.option));
      continue;
    }
    if (std::ranges::find(available, item.analyzer) == available.end()) {
      messages.push_back(std::format(
          "Invalid argument to --analyzer-config: '{}' is not an available "
          "analyzer. Available analyzers are: {}.",
          item.analyzer, util::Join(available, ", ")));
      continue;
    }
    const std::vector<AnalyzerOption>& known = options[item.analyzer];
    const auto option =
        std::ranges::find(known, item.option, &AnalyzerOption::option);
    if (option == known.end()) {
      messages.push_back(std::format(
          "Invalid argument to --analyzer-config: {} has no config named "
          "'{}'. Use the 'CodeChecker analyzers --analyzer-config {}' command "
          "to see available configs.",
          item.analyzer, item.option, item.analyzer));
    } else if (option->must_exist && !util::pypath::Exists(item.value)) {
      messages.push_back(std::format(
          "Invalid value to --analyzer-config: '{}:{}={}'. File does not "
          "exist: {}",
          item.analyzer, item.option, item.value,
          util::pypath::Abspath(item.value)));
    }
  }
  for (const std::string& message : messages) kLog.Error(message);
  return messages.empty();
}

// Python: is_checker_config_valid().
bool IsCheckerConfigValid(const std::vector<CheckerConfigArg>& items) {
  std::vector<std::string> messages;
  std::vector<std::string> names;
  std::map<std::string, std::vector<CheckerOption>> options;
  for (const AnalyzerDriver* analyzer : SupportedAnalyzers()) {
    names.emplace_back(analyzer->name());
    options[std::string(analyzer->name())] = analyzer->CheckerConfigOptions();
  }
  for (const CheckerConfigArg& item : items) {
    if (std::ranges::find(names, item.analyzer) == names.end()) {
      messages.push_back(std::format(
          "Invalid argument to --checker-config: '{}' is not a supported "
          "analyzer. Supported analyzers are: {}.",
          item.analyzer, util::Join(names, ", ")));
      continue;
    }
    const std::vector<CheckerOption>& known = options[item.analyzer];
    const bool found = std::ranges::any_of(known, [&item](const auto& option) {
      return option.checker == item.checker && option.option == item.option;
    });
    if (!found) {
      messages.push_back(std::format(
          "Invalid argument to --checker-config: invalid checker '{}' and/or "
          "checker option '{}' for {}. Use the 'CodeChecker checkers "
          "--checker-config' command to see available checker options.",
          item.checker, item.option, item.analyzer));
    }
  }
  for (const std::string& message : messages) kLog.Error(message);
  return messages.empty();
}

// Python: check_satisfied_capabilities().
Result<void> CheckSatisfiedCapabilities(const AnalyzeArgs& args) {
  bool has_error = false;
  const bool ctu_options =
      args.ctu_ast_mode || args.ctu_phases || args.ctu_reanalyze_on_failure;
  const bool stats_options = args.stats_dir || args.stats_enabled ||
                             args.stats_min_sample_count || args.stats_output ||
                             args.stats_relevance_threshold;
  // Python looks at the options in alphabetical order: CTU ones first.
  if (ctu_options && !ClangSaAnalyzer().IsCtuCapable()) {
    kLog.Error("CrossTranslation Unit options can only be enabled if clang "
               "itself supports it!");
    kLog.Info("hint: Clang 8.0.0 is the earliest version to support it");
    has_error = true;
  } else if (stats_options && !IsStatisticsCapable()) {
    kLog.Error("Statistics options can only be enabled if clang has "
               "statistics checkers available!");
    kLog.Info("CodeChecker has not found Clang Static Analyzer checkers "
              "statisticsCollector.ReturnValueCheck and "
              "statisticsCollector.SpecialReturnValue");
    has_error = true;
  }
  if (args.ctu_ast_mode && !ClangSaAnalyzer().IsOnDemandCtuAvailable()) {
    kLog.Error("Clang does not support on-demand Cross Translation "
               "Unitanalysis!");
    kLog.Info("hint: Clang 11.0.0 is the earliest version to support it");
    has_error = true;
  }
  if (args.enable_z3_refutation == "on" && !IsZ3Capable()) {
    kLog.Error("Z3 refutation cannot be enabled as Clang was not compiled "
               "with Z3!");
    has_error = true;
  }
  if (has_error) {
    kLog.Info("hint: Maybe CodeChecker found the wrong analyzer binary?");
    return Result<void>(Error{});
  }
  return Result<void>();
}

// Python: get_affected_file_paths(): the --file patterns, with the sources
// that include the given headers.
std::vector<std::string> AffectedFilePaths(
    const std::vector<std::string>& file_filters,
    const std::vector<tu_collector::CompileAction>& compile_commands,
    int jobs) {
  constexpr std::string_view kHeaderExtensions[] = {
      ".h", ".hh", ".H", ".hp", ".hxx", ".hpp", ".HPP", ".h++", ".tcc"};
  std::vector<std::string> paths;
  std::vector<std::string> headers;
  for (const std::string& filter : file_filters) {
    paths.push_back(filter.find('*') == std::string::npos
                        ? util::pypath::Realpath(filter)
                        : filter);
    if (util::pypath::Exists(filter) &&
        std::ranges::any_of(kHeaderExtensions, [&filter](std::string_view ext) {
          return filter.ends_with(ext);
        })) {
      headers.push_back(filter);
    }
  }
  if (headers.empty()) return paths;
  const std::string joined = util::Join(headers, ", ");
  kLog.Info("Get dependent source files for '" + joined + "'...");
  const std::set<std::string> sources =
      tu_collector::GetDependentSources(compile_commands, headers, jobs);
  kLog.Info("Get dependent source files for '" + joined + "' done.");
  kLog.Debug(
      "Dependent source files: " +
      util::Join(std::vector<std::string>(sources.begin(), sources.end()),
                 ", "));
  paths.insert(paths.end(), sources.begin(), sources.end());
  return paths;
}

// Python: __get_skip_handlers().
Result<common::SkipListHandlers> SkipHandlers(
    const AnalyzeArgs& args,
    const std::vector<tu_collector::CompileAction>& compile_commands) {
  common::SkipListHandlers handlers;
  if (args.files) {
    std::vector<std::string> lines;
    for (const std::string& path :
         AffectedFilePaths(*args.files, compile_commands, args.jobs)) {
      lines.push_back("+" + path);
    }
    // Every header is kept, the other sources are skipped.
    lines.insert(lines.end(), {"+/*.h", "+/*.H", "+/*.tcc", "-*"});
    const std::string content = util::Join(lines, "\n");
    handlers.emplace_back(content);
    kLog.Debug("Skip handler is created for the '--file' option with the "
               "following filters:\n" +
               content);
  }
  if (args.skipfile) {
    const Result<std::string> content = util::ReadFile(*args.skipfile);
    if (!content.ok()) {
      util::Print(content.error().message);
      return Result<common::SkipListHandlers>(Error{});
    }
    const std::string text = util::TranslateNewlines(
        util::DecodeUtf8Ignore(content.value()));
    handlers.emplace_back(text);
    kLog.Debug("Skip handler is created for the '--ignore' option with the "
               "following filters:\n" +
               text);
  }
  return Result<common::SkipListHandlers>(std::move(handlers));
}

// Copies `source` to `destination` (Python: shutil.copyfile()). A failure,
// which ends the Python version with an exception, is logged.
void CopySideFile(const std::string& source, const std::string& destination) {
  if (const std::error_code error = platform::CopyFile(source, destination)) {
    kLog.Error(util::OsError(error.value(), source).message);
  }
}

// Python: __update_skip_file(), __update_review_status_config() and
// __update_analysis_config_files().
void UpdateSideFiles(const AnalyzeArgs& args) {
  const std::string skip_file =
      util::pypath::Join({args.output_path, "skip_file"});
  if (util::pypath::Exists(skip_file)) std::remove(skip_file.c_str());
  if (args.skipfile) {
    kLog.Debug("Copying skip file " + *args.skipfile + " to " + skip_file);
    CopySideFile(*args.skipfile, skip_file);
  }
  const std::string review_status =
      util::pypath::Join({args.output_path, "review_status.yaml"});
  if (util::pypath::Exists(review_status)) std::remove(review_status.c_str());
  if (args.review_status_config) {
    kLog.Debug("Copying review status config file " +
               *args.review_status_config + " to " + review_status);
    CopySideFile(*args.review_status_config, review_status);
  }
  const std::string conf_dir = util::pypath::Join({args.output_path, "conf"});
  if (util::pypath::IsDir(conf_dir)) platform::RemoveTree(conf_dir);
  if (const std::error_code error = platform::MakeDirs(conf_dir)) {
    kLog.Error(util::OsError(error.value(), conf_dir).message);
    return;
  }
  const auto add = [&conf_dir](const std::string& path) {
    if (!util::pypath::IsFile(path)) return;
    const std::string name =
        util::pypath::Basename(util::pypath::Abspath(path));
    CopySideFile(path, util::pypath::Join({conf_dir, name}));
  };
  if (args.skipfile && !args.skipfile->empty()) add(*args.skipfile);
  if (args.review_status_config && !args.review_status_config->empty()) {
    add(*args.review_status_config);
  }
  if (args.config_file && !args.config_file->empty()) add(*args.config_file);
  for (const AnalyzerConfigArg& item : args.analyzer_config) {
    if (item.option == "cc-verbatim-args-file") add(item.value);
  }
}

// Python: __get_statistics_data(); nullopt if statistics are off or the
// settings are wrong (logged).
std::optional<StatisticsData> GetStatisticsData(const AnalyzeArgs& args) {
  std::optional<StatisticsData> data;
  if (args.stats_enabled) {
    data = StatisticsData{
        .stats_out_dir = util::pypath::Join({args.output_path, "stats"})};
  }
  if (args.stats_output && !args.stats_output->empty()) {
    data = StatisticsData{.stats_out_dir = *args.stats_output};
  }
  if (!data) return std::nullopt;
  data->stat_tmp_dir = util::pypath::Join({data->stats_out_dir, "tmp"});
  if (args.stats_min_sample_count) {
    if (*args.stats_min_sample_count <= 1) {
      kAnalyzerLog.Error("stats_min_sample_countmust be greater than 1.");
      return std::nullopt;
    }
    data->min_sample_count = args.stats_min_sample_count;
  }
  if (args.stats_relevance_threshold) {
    if (!(*args.stats_relevance_threshold > 0 &&
          *args.stats_relevance_threshold < 1)) {
      kAnalyzerLog.Error("stats-relevance-threshold must be greater than 0 "
                         "and smaller than 1.");
      return std::nullopt;
    }
    data->relevance_threshold = args.stats_relevance_threshold;
  }
  return data;
}

double Now() {
  return std::chrono::duration<double>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Python: analyzer.perform_analysis(). Errors are logged and have exit
// code 1.
Result<void> PerformAnalysis(const AnalyzeArgs& args,
                             const common::SkipListHandlers& skip_handlers,
                             const common::SkipListHandlers* filter_handlers,
                             const common::ReviewStatusHandler& review_status,
                             const std::vector<BuildAction>& build_actions,
                             const CompileCommandCount& count,
                             MetadataTool* metadata) {
  const Result<AnalyzerAvailability> availability =
      CheckAvailableAnalyzers(args.analyzers);
  if (!availability.ok()) return Result<void>(availability.error());
  // Registry order: the longer running analyzers are scheduled first.
  std::vector<std::string> analyzers;
  for (const AnalyzerDriver* analyzer : SupportedAnalyzers()) {
    if (std::ranges::find(availability.value().enabled, analyzer->name()) !=
        availability.value().enabled.end()) {
      analyzers.emplace_back(analyzer->name());
    }
  }
  const bool has_clangsa =
      std::ranges::find(analyzers, "clangsa") != analyzers.end();
  bool ctu_collect = false;
  bool ctu_analyze = false;
  std::string ctu_dir;
  if (args.ctu_phases) {
    ctu_dir = util::pypath::Join({args.output_path, "ctu-dir"});
    if (!has_clangsa) {
      kAnalyzerLog.Error(
          "CTU can only be used with the clang static analyzer.");
      return Result<void>();
    }
    ctu_collect = args.ctu_phases->collect;
    ctu_analyze = args.ctu_phases->analyze;
  }
  if (args.stats_enabled && !has_clangsa) {
    kAnalyzerLog.Debug("Statistics can only be used with the Clang Static "
                       "Analyzer.");
    return Result<void>();
  }

  ConfigArgs config_args = {
      .report_hash = args.report_hash,
      .enable_z3_refutation = args.enable_z3_refutation == "on",
      .add_gcc_include_dirs_with_isystem =
          args.add_gcc_include_dirs_with_isystem,
      .ctu_dir = ctu_dir,
      .ctu_on_demand =
          args.ctu_phases && args.ctu_ast_mode == "parse-on-demand",
      .ordered_checkers =
          args.ordered_checkers.value_or(std::vector<CheckerChoice>()),
      .enable_all = args.enable_all,
      .stats_enabled = args.stats_enabled,
      .analyzer_config = args.analyzer_config,
      .checker_config = args.checker_config};
  Result<std::map<std::string, AnalyzerConfig>> built =
      BuildConfigs(config_args, analyzers);
  if (!built.ok()) return Result<void>(built.error());
  const std::map<std::string, AnalyzerConfig>& configs = built.value();

  // Analyzers without enabled checkers do not run; clang-tidy would fail.
  std::erase_if(analyzers, [&configs](const std::string& name) {
    if (configs.at(name).checkers.HasEnabled()) return false;
    kAnalyzerLog.Info("No checkers enabled for " + name);
    return true;
  });

  std::vector<BuildAction> actions;
  for (const std::string& analyzer : analyzers) {
    for (const BuildAction& action : build_actions) {
      actions.push_back(action);
      actions.back().analyzer_type = analyzer;
    }
  }

  // Unknown -e/-d names: they match no checker and no profile.
  if (args.ordered_checkers) {
    const Result<AnalyzerContext*> context = GetContext();
    std::vector<std::string> available;
    for (const auto& [profile, description] :
         context.value()->checker_labels().Description("profile")) {
      available.push_back(profile);
    }
    for (const auto& [name, config] : configs) {
      for (const CheckerEntry& checker : config.checkers.entries()) {
        available.push_back(checker.name);
      }
    }
    std::vector<std::string> missing_prefixes;
    std::vector<std::string> missing_checkers;
    for (const auto& [raw_name, enabled] : *args.ordered_checkers) {
      if (raw_name.starts_with("prefix:")) {
        const std::string prefix = raw_name.substr(7);
        const bool found =
            std::ranges::find(available, prefix) != available.end() ||
            std::ranges::any_of(available, [&prefix](const std::string& item) {
              return item.starts_with(prefix + ".") ||
                     item.starts_with(prefix + "-");
            });
        if (!found) missing_prefixes.push_back(prefix);
      }
      std::string name = raw_name;
      if (name.starts_with("checker:")) {
        for (size_t pos = name.find("checker:"); pos != std::string::npos;
             pos = name.find("checker:", pos)) {
          name.erase(pos, 8);
        }
      }
      if (name.find(':') != std::string::npos) continue;
      const bool found =
          std::ranges::any_of(available, [&name](const std::string& item) {
            return MatchesCheckerPrefix(name, item);
          });
      if (!found && std::ranges::find(missing_checkers, name) ==
                        missing_checkers.end()) {
        missing_checkers.push_back(name);
      }
    }
    bool any_missing = false;
    for (const auto& [list_name, items] :
         {std::pair<std::string_view, const std::vector<std::string>*>(
              "prefix(es)", &missing_prefixes),
          {"checker(s)", &missing_checkers}}) {
      if (items->empty()) continue;
      any_missing = true;
      const std::string message =
          std::format("No {} with these names was found: {}.", list_name,
                      util::Join(*items, ", "));
      if (args.no_missing_checker_error) {
        kAnalyzerLog.Warning(message);
      } else {
        kAnalyzerLog.Error(message);
      }
    }
    if (any_missing && !args.no_missing_checker_error) {
      kAnalyzerLog.Info(
          "Although it is not recommended, if you want to suppress errors "
          "relating to unknown checker names, consider using the option "
          "'--no-missing-checker-error'");
      return Result<void>(Error{});
    }
  }

  for (const std::string& analyzer : analyzers) {
    AnalyzerMetadata data;
    for (const CheckerEntry& checker :
         configs.at(analyzer).checkers.entries()) {
      data.checkers.push_back(
          {.name = checker.name,
           .enabled = checker.state == CheckerState::kEnabled});
    }
    const std::optional<util::SemVer> version =
        FindAnalyzer(analyzer)->Version();
    data.statistics.version = version ? version->ToString() : "None";
    metadata->analyzers.emplace_back(analyzer, std::move(data));
  }
  kAnalyzerLog.Info("Enabled checker list can be found in " +
                    util::pypath::Join({args.output_path, "metadata.json"}));

  const auto ctu_data = [&ctu_dir]() {
    return CtuData{.ctu_dir = ctu_dir,
                   .mapping = ClangSaAnalyzer().CtuMapping()};
  };
  if (args.makefile) {
    const std::optional<StatisticsData> statistics = GetStatisticsData(args);
    std::optional<CtuData> ctu;
    if (ctu_collect || statistics) ctu = ctu_data();
    CreateMakefile(analyzers, args.output_path, configs, ctu_collect,
                   statistics, ctu, actions);
    return Result<void>();
  }

  if (ctu_collect) {
    platform::RemoveTree(ctu_dir);
  } else if (ctu_analyze && !util::pypath::Exists(ctu_dir)) {
    kAnalyzerLog.Error("CTU directory: '" + ctu_dir + "' does not exist.");
    return Result<void>();
  }
  const double start_time = Now();
  std::optional<StatisticsData> statistics = GetStatisticsData(args);
  if (ctu_collect || statistics) {
    std::optional<CtuData> ctu;
    if (ctu_collect || ctu_analyze) ctu = ctu_data();
    std::vector<const BuildAction*> pre_analyze;
    for (const BuildAction& action : actions) {
      if (action.analyzer_type == "clangsa") pre_analyze.push_back(&action);
    }
    // The skip list applies to the pre-analysis only if just the collect
    // phase was asked for.
    const common::SkipListHandlers* pre_skip_handlers = nullptr;
    if ((ctu_collect && !ctu_analyze) ||
        (args.stats_output && !args.stats_output->empty())) {
      pre_skip_handlers = &skip_handlers;
    }
    if (const auto clangsa = configs.find("clangsa");
        clangsa != configs.end()) {
      RunPreAnalysis(pre_analyze, clangsa->second, args.jobs, pre_skip_handlers,
                     ctu, statistics);
    } else {
      kAnalyzerLog.Error("Can not run pre analysis without clang static "
                         "analyzer configuration.");
    }
  }
  if (args.stats_output && !args.stats_output->empty()) return Result<void>();
  if (args.stats_dir && !args.stats_dir->empty()) {
    statistics = StatisticsData{.stats_out_dir = *args.stats_dir};
  }
  if (ctu_analyze || statistics || (!ctu_analyze && !ctu_collect)) {
    kAnalyzerLog.Info("Starting static analysis ...");
    StartWorkers(actions, configs,
                 {.jobs = args.jobs,
                  .output_path = args.output_path,
                  .skip_handlers = &skip_handlers,
                  .filter_handlers = filter_handlers,
                  .review_status = &review_status,
                  .quiet = args.quiet,
                  .capture_analysis_output = args.capture_analysis_output,
                  .generate_reproducer = args.generate_reproducer,
                  .timeout = args.timeout,
                  .ctu_reanalyze_on_failure = args.ctu_reanalyze_on_failure,
                  .statistics = statistics},
                 count, metadata);
    kAnalyzerLog.Info("Analysis finished.");
    kAnalyzerLog.Info("To view results in the terminal use the \"CodeChecker "
                      "parse\" command.");
    kAnalyzerLog.Info("To store results use the \"CodeChecker store\" "
                      "command.");
    kAnalyzerLog.Info("See --help and the user guide for further options about "
                      "parsing and storing the reports.");
    kAnalyzerLog.Info("----=================----");
  }
  const double end_time = Now();
  kAnalyzerLog.Info(
      "Analysis length: " +
      util::FloatRepr(std::round((end_time - start_time) * 100) / 100) +
      " sec.");
  metadata->timestamps = Timestamps{.begin = start_time, .end = end_time};
  if (ctu_collect && ctu_analyze) platform::RemoveTree(ctu_dir);
  return Result<void>();
}

// Checks the options of `args` and completes them: the output path made
// absolute, --disable-all as a leading "-d default", and alpha uniqueing for
// CTU (Python: the checks at the start of analyze.main()). Problems are
// logged.
Result<void> CheckArgs(AnalyzeArgs* args) {
  bool configs_valid = IsAnalyzerConfigValid(*args);
  if (args->checker_config) {
    configs_valid =
        IsCheckerConfigValid(*args->checker_config) && configs_valid;
  }
  if (!configs_valid && !args->no_missing_checker_error) {
    kLog.Info("Although it is not recommended, if you want to suppress errors "
              "relating to unknown analyzer/checker configs, consider using "
              "the option '--no-missing-checker-error'");
    return Result<void>(Error{});
  }
  if (args->ctu_ast_mode && !args->ctu_phases) {
    kLog.Error("Analyzer option 'ctu-ast-mode' requires CTU mode enabled");
    return Result<void>(Error{});
  }
  if (Result<void> capable = CheckSatisfiedCapabilities(*args);
      !capable.ok()) {
    return capable;
  }
  if (args->config_file && !args->config_file->empty() &&
      !util::pypath::Exists(*args->config_file)) {
    kLog.Error("Configuration file '" + *args->config_file +
               "' does not exist.");
    return Result<void>(Error{});
  }
  args->output_path = util::pypath::Abspath(args->output_path);
  if (util::pypath::Exists(args->output_path) &&
      !util::pypath::IsDir(args->output_path) &&
      !args->dump_compiler_info_file) {
    kLog.Error("The given output path is not a directory: " +
               args->output_path);
    return Result<void>(Error{});
  }
  if (args->enable_all) {
    kLog.Info("'--enable-all' was supplied for this analysis.");
  }
  if (args->disable_all) {
    if (!args->ordered_checkers) args->ordered_checkers.emplace();
    args->ordered_checkers->insert(args->ordered_checkers->begin(),
                                   {.name = "default", .enabled = false});
  }
  // CTU works well only with one action per source file.
  if (args->compile_uniqueing.find("none") != std::string::npos &&
      args->ctu_phases) {
    args->compile_uniqueing = "alpha";
  }
  if (args->compiler_info_file) {
    kLog.Debug("Compiler info is read from: " + *args->compiler_info_file);
    if (!util::pypath::Exists(*args->compiler_info_file)) {
      kLog.Error("Compiler info file " + *args->compiler_info_file +
                 " does not exist");
      return Result<void>(Error{});
    }
  }
  return Result<void>();
}

// Returns the file, command and directory of each entry of the compilation
// database `entries`; missing members are empty.
std::vector<tu_collector::CompileAction> CompileActions(
    yyjson_mut_val* entries) {
  std::vector<tu_collector::CompileAction> actions;
  yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(entries);
  while (yyjson_mut_val* entry = yyjson_mut_arr_iter_next(&iter)) {
    const auto member = [entry](const char* key) {
      return util::JsonString(yyjson_mut_obj_get(entry, key));
    };
    actions.push_back({.file = member("file"),
                       .command = member("command"),
                       .directory = member("directory")});
  }
  return actions;
}

// Returns the settings of ParseUniqueLog() for `args`. `skip_handlers` also
// skip the pre-analysis of CTU collection and statistics.
LogParseOptions MakeLogParseOptions(
    const AnalyzeArgs& args, const common::SkipListHandlers* skip_handlers) {
  LogParseOptions options = {
      .compile_uniqueing = args.compile_uniqueing,
      .compiler_info_file = args.compiler_info_file,
      .keep_gcc_include_fixed = args.keep_gcc_include_fixed,
      .keep_gcc_intrin = args.keep_gcc_intrin,
      .jobs = args.jobs,
      .analysis_skip_handlers = skip_handlers,
      .ctu_or_stats_enabled = args.stats_enabled};
  if (args.ctu_phases) {
    if (args.ctu_phases->collect && !args.ctu_phases->analyze) {
      options.pre_analysis_skip_handlers = skip_handlers;
    }
    if (args.ctu_phases->collect || args.ctu_phases->analyze) {
      options.ctu_or_stats_enabled = true;
    }
  }
  if (args.stats_output && !args.stats_output->empty()) {
    options.pre_analysis_skip_handlers = skip_handlers;
    options.ctu_or_stats_enabled = true;
  }
  return options;
}

// Removes the CTU folder before a new collection, removes the output folder
// for --clean, and creates the output folder if it is missing.
Result<void> PrepareOutputDir(const AnalyzeArgs& args) {
  const std::string ctu_dir = util::pypath::Join({args.output_path, "ctu-dir"});
  if (args.ctu_phases && args.ctu_phases->collect &&
      util::pypath::IsDir(ctu_dir)) {
    kLog.Debug("Previous CTU contents have been deleted.");
    platform::RemoveTree(ctu_dir);
  }
  if (args.clean && util::pypath::IsDir(args.output_path)) {
    kLog.Info("Previous analysis results in '" + args.output_path +
              "' have been removed, overwriting with current result");
    platform::RemoveTree(args.output_path);
  }
  if (!util::pypath::Exists(args.output_path) &&
      !args.dump_compiler_info_file) {
    if (const std::error_code error = platform::MakeDirs(args.output_path)) {
      kLog.Error(util::OsError(error.value(), args.output_path).message);
      return Result<void>(Error{});
    }
  }
  kLog.Debug("Output will be stored to: '" + args.output_path + "'");
  return Result<void>();
}

// Writes the command, directory and file of `actions` into
// <output_path>/unique_compile_commands.json.
void WriteUniqueCompileCommands(const std::string& output_path,
                                const std::vector<BuildAction>& actions) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* list = yyjson_mut_arr(doc);
  for (const BuildAction& action : actions) {
    yyjson_mut_val* entry = yyjson_mut_arr_add_obj(doc, list);
    yyjson_mut_obj_add_strncpy(doc, entry, "command",
                               action.original_command.data(),
                               action.original_command.size());
    yyjson_mut_obj_add_strncpy(doc, entry, "directory",
                               action.directory.data(),
                               action.directory.size());
    yyjson_mut_obj_add_strncpy(doc, entry, "file", action.source.data(),
                               action.source.size());
  }
  (void)util::WriteFile(
      util::pypath::Join({output_path, "unique_compile_commands.json"}),
      util::DumpJson(list));
}

// Returns the numbers of compile commands, and logs them: `total` in the
// database, `skipped` while parsing it, and `analyzed` after uniqueing.
CompileCommandCount CountCompileCommands(int total, int skipped,
                                         int analyzed) {
  const CompileCommandCount count = {
      .total = total,
      .analyze = analyzed,
      .skipped = skipped,
      .removed_by_uniqueing = total - skipped - analyzed};
  kLog.Debug(std::format("Total number of compile commands without skipping "
                         "or uniqueing: {}",
                         count.total));
  kLog.Debug(std::format("Compile commands removed by uniqueing: {}",
                         count.removed_by_uniqueing));
  kLog.Debug(std::format("Compile commands skipped during log processing: {}",
                         count.skipped));
  kLog.Debug(std::format("Compile commands forwarded for analysis: {}",
                         count.analyze));
  return count;
}

// Removes the result files in `previous` (result file -> source file, from
// the metadata of an earlier analysis; nullopt if there is none) whose
// source file no longer exists, and their entries in `metadata`.
void RemoveResultsOfDeletedSources(
    const std::optional<std::vector<std::pair<std::string, std::string>>>&
        previous,
    MetadataTool* metadata) {
  kLog.Debug("Cleanup metadata file started.");
  for (const auto& [plist, source] : previous.value_or(
           std::vector<std::pair<std::string, std::string>>())) {
    if (util::pypath::Exists(source)) continue;
    kLog.Info("Remove plist file '" + plist +
              "' because it refers to a source file ('" + source +
              "') which was removed.");
    RemoveResultSource(plist, metadata);
    if (std::remove(plist.c_str()) != 0) {
      kLog.Warning("Failed to remove plist file: " + plist);
    }
  }
  kLog.Debug("Cleanup metadata file finished.");
}

// Python: analyze.main(), after the logger is set up.
Result<int> AnalyzeMain(AnalyzeArgs args) {
  if (Result<void> checked = CheckArgs(&args); !checked.ok()) {
    return Result<int>(checked.error());
  }
  Result<std::optional<util::MutableJsonDocument>> gathered =
      GatherCompilationDatabase(args.input);
  if (!gathered.ok()) return Result<int>(gathered.error());
  if (!gathered.value()) {
    kLog.Error("Found no compilation commands in '" + args.input + "'");
    return Result<int>(Error{});
  }
  util::MutableJsonDocument database = std::move(*gathered.value());
  yyjson_mut_doc* doc = database.get();
  yyjson_mut_val* root = yyjson_mut_doc_get_root(doc);
  // An empty object is an empty database.
  yyjson_mut_val* entries =
      yyjson_mut_is_arr(root) ? root : yyjson_mut_arr(doc);

  Result<common::SkipListHandlers> skip_handlers =
      SkipHandlers(args, CompileActions(entries));
  if (!skip_handlers.ok()) return Result<int>(skip_handlers.error());
  const common::SkipListHandlers* filter_handlers =
      args.drop_skipped_reports ? &skip_handlers.value() : nullptr;
  common::ReviewStatusHandler review_status;
  if (args.review_status_config) {
    if (Result<void> loaded =
            review_status.SetReviewStatusConfig(*args.review_status_config);
        !loaded.ok()) {
      kLog.Error(loaded.error().message);
      return Result<int>(Error{});
    }
  }
  const Result<AnalyzerContext*> context = GetContext();
  if (!context.ok()) return Result<int>(context.error());
  if (Result<void> prepared = PrepareOutputDir(args); !prepared.ok()) {
    return Result<int>(prepared.error());
  }

  ImplicitCompilerInfo compiler_info;
  const Result<ParsedLog> parsed = ParseUniqueLog(
      doc, entries, MakeLogParseOptions(args, &skip_handlers.value()),
      &compiler_info);
  if (!parsed.ok()) return Result<int>(parsed.error());
  const std::vector<BuildAction>& actions = parsed.value().actions;
  if (actions.empty()) {
    kLog.Warning("No analysis is required.");
    kLog.Warning("There were no compilation commands in the provided "
                 "compilation database or all of them were skipped.");
    // Python: sys.exit(0), so `check` does not parse.
    return Result<int>(Error{.exit_code = 0});
  }
  if (args.dump_compiler_info_file) {
    // Python ends with an uncaught exception here.
    if (const Result<void> written =
            util::WriteFile(args.output_path, compiler_info.Dump());
        !written.ok()) {
      util::Print(written.error().message);
      return Result<int>(Error{});
    }
    return Result<int>(Error{.exit_code = 0});
  }
  (void)util::WriteFile(
      util::pypath::Join({args.output_path, "compiler_info.json"}),
      compiler_info.Dump());
  const std::string fixit_dir = util::pypath::Join({args.output_path, "fixit"});
  if (const std::error_code error = platform::EnsureDirs(fixit_dir)) {
    kLog.Error(util::OsError(error.value(), fixit_dir).message);
    return Result<int>(Error{});
  }
  WriteUniqueCompileCommands(args.output_path, actions);

  MetadataTool metadata;
  metadata.action_num = static_cast<int>(actions.size());
  metadata.command = common::CommandLine();
  metadata.version = context.value()->git_tag().value_or("None") + " (" +
                     context.value()->git_hash().value_or("None") + ")";
  metadata.working_directory = util::pypath::Abspath(".");
  metadata.output_path = args.output_path;
  metadata.run_name = args.name;
  const std::string metadata_file =
      util::pypath::Join({args.output_path, "metadata.json"});
  std::optional<std::vector<std::pair<std::string, std::string>>> previous;
  if (util::pypath::Exists(metadata_file)) {
    previous = ReadResultSourceFiles(metadata_file);
    if (previous) metadata.result_source_files = *previous;
  }
  const CompileCommandCount count = CountCompileCommands(
      static_cast<int>(yyjson_mut_arr_size(entries)), parsed.value().skipped,
      static_cast<int>(actions.size()));

  if (Result<void> performed =
          PerformAnalysis(args, skip_handlers.value(), filter_handlers,
                          review_status, actions, count, &metadata);
      !performed.ok()) {
    return Result<int>(performed.error());
  }
  UpdateSideFiles(args);
  RemoveResultsOfDeletedSources(previous, &metadata);
  kLog.Debug("Analysis metadata write to '" + metadata_file + "'");
  (void)util::WriteFile(metadata_file, MetadataJson(metadata));
  (void)util::WriteFile(
      util::pypath::Join({args.output_path, "compile_cmd.json"}),
      util::DumpJsonIndent(root, 2));

  for (const auto& [name, analyzer] : metadata.analyzers) {
    if (analyzer.statistics.failed != 0) return Result<int>(3);
  }
  return Result<int>(0);
}

// Ends the program with exit code 1 if the analyzer context cannot be set
// up, as Python does. The argument defaults that run Clang need it.
void ExitIfNoContext() {
  if (!GetContext().ok()) {
    util::Flush(util::Stream::kStdout);
    std::exit(1);
  }
}

}  // namespace

void AddJobsArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"-j", "--jobs"},
       .dest = "jobs",
       .type = argparse::IntType,
       .default_value = std::to_string(platform::CpuCount()),
       .help = "Number of threads to use in analysis. More threads mean "
               "faster analysis at the cost of using more memory.",
       .group = group});
}

void AddCleanArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"-c", "--clean"},
       .dest = "clean",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Delete analysis reports stored in the output directory. (By "
               "default, CodeChecker would keep reports and overwrites only "
               "those files that were update by the current build command).",
       .group = group});
}

void AddOutputFormatArgument(argparse::Parser& parser) {
  parser.AddArgument(
      {.flags = {"-t", "--type", "--output-format"},
       .dest = "output_format",
       .choices = {"plist"},
       .default_value = "plist",
       .help = "Specify the format the analysis results should use."});
}

void AddCompileUniqueingArgument(argparse::Parser& parser) {
  parser.AddArgument({.flags = {"--compile-uniqueing"},
                      .dest = "compile_uniqueing",
                      .default_value = "none",
                      .help = std::string(kCompileUniqueingHelp)});
}

void AddReportHashArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"--report-hash"},
       .dest = "report_hash",
       .choices = {"context-free", "context-free-v2", "diagnostic-message"},
       .help = std::string(kReportHashHelp),
       .group = group});
}

void AddReviewStatusConfigArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"--review-status-config"},
       .dest = "review_status_config",
       .type = ExistingAbspath,
       .help = "Path of review_status.yaml config file which contains review "
               "status settings assigned to specific directories, checkers, "
               "bug hashes.",
       .group = group});
}

void AddZ3RefutationArgument(argparse::Parser& parser, int group) {
  ExitIfNoContext();
  const bool z3_refutation = IsZ3RefutationCapable();
  parser.AddArgument(
      {.flags = {"--z3-refutation"},
       .dest = "enable_z3_refutation",
       .choices = {"on", "off"},
       .default_value = z3_refutation ? "on" : "off",
       .help = "Switch on/off the Z3 SMT Solver backend to reduce false "
               "positives. The results of the ranged based constraint solver "
               "in the Clang Static Analyzer will be cross checked with the Z3 "
               "SMT solver. This should not cause that much of a slowdown "
               "compared to using only the Z3 solver.",
       .group = group});
}

void AddCompilerInfoArguments(argparse::Parser& parser) {
  parser.AddArgument(
      {.flags = {"--compiler-info-file"},
       .dest = "compiler_info_file",
       .help = "Read the compiler includes and target from the specified "
               "file rather than invoke the compiler executable."});
  parser.AddArgument(
      {.flags = {"--dump-compiler-info-file"},
       .dest = "dump_compiler_info_file",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "Dump implicit gcc compiler info to a json file that can be "
               "used for fine-tuning analysis later.These are information "
               "like the implicit include paths of standard headers, the "
               "default language version and the default target "
               "architecture."});
  parser.AddArgument(
      {.flags = {"--keep-gcc-include-fixed"},
       .dest = "keep_gcc_include_fixed",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "There are some implicit include paths which are only used by "
               "GCC (include-fixed). This flag determines whether these "
               "should be kept among the implicit include paths."});
  parser.AddArgument(
      {.flags = {"--keep-gcc-intrin"},
       .dest = "keep_gcc_intrin",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "There are some implicit include paths which contain "
               "GCC-specific header files (those which end with intrin.h). "
               "This flag determines whether these should be kept among the "
               "implicit include paths. Use this flag if Clang analysis fails "
               "with error message related to __builtin symbols."});
  parser.AddArgument(
      {.flags = {"--add-gcc-include-dirs-with-isystem"},
       .dest = "add_gcc_include_dirs_with_isystem",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "Implicit include directories are appended to the analyzer "
               "command with -idirafter. If -isystem is needed instead, as it "
               "was used before CodeChecker 6.24.1, this flag can be used."});
}

void AddFileFilterArguments(argparse::Parser& parser) {
  const int skip_group = parser.AddArgumentGroup("file filter arguments", "");
  parser.AddArgument(
      {.flags = {"-i", "--ignore", "--skip"},
       .dest = "skipfile",
       .help = "Path to the Skipfile dictating which project files should be "
               "omitted from analysis. Please consult the User guide on how a "
               "Skipfile should be laid out.",
       .group = skip_group});
  parser.AddArgument(
      {.flags = {"--drop-reports-from-skipped-files"},
       .dest = "drop_skipped_reports",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "Filter our reports from files that were  skipped from the "
               "analysis.",
       .group = skip_group});
  parser.AddArgument(
      {.flags = {"--file"},
       .dest = "files",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "FILE",
       .help = "Analyze only the given file(s) not the whole compilation "
               "database. Absolute directory paths should start with '/', "
               "relative directory paths should start with '*' and it can "
               "contain path glob pattern. Example: '/path/to/main.cpp', "
               "'lib/*.cpp', */test*'.",
       .group = skip_group});
}

void AddAnalyzerArguments(argparse::Parser& parser, int group) {
  const std::vector<std::string> analyzer_names = SupportedAnalyzerNames();
  parser.AddArgument(
      {.flags = {"--analyzers"},
       .dest = "analyzers",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "ANALYZER",
       .choices = analyzer_names,
       .default_none = true,
       .help = "Run analysis only with the analyzers specified. Currently "
               "supported analyzers are: " +
               util::Join(analyzer_names, ", ") + ".",
       .group = group});
  parser.AddArgument(
      {.flags = {"--capture-analysis-output"},
       .dest = "capture_analysis_output",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Store standard output and standard error of successful "
               "analyzer invocations into the '<OUTPUT_DIR>/success' "
               "directory.",
       .group = group});
  parser.AddArgument(
      {.flags = {"--generate-reproducer"},
       .dest = "generate_reproducer",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Collect all necessary information for reproducing an "
               "analysis action. The gathered files will be stored in a "
               "folder named 'reproducer' under the report directory. When "
               "this flag is used, 'failed' directory remains empty.",
       .group = group});
  common::AddConfigOption(parser, group);
  parser.AddArgument(
      {.flags = {"--cppcheckargs"},
       .dest = "cppcheck_args_cfg_file",
       .help = "DEPRECATED. File containing argument which will be forwarded "
               "verbatim for Cppcheck. The option has been migrated under the "
               "cppcheck anayzer options: --analyzer-config "
               "cppcheck:cc-verbatim-args-file=<filepath>",
       .group = group});
  parser.AddArgument(
      {.flags = {"--saargs"},
       .dest = "clangsa_args_cfg_file",
       .help = "DEPRECATED. File containing argument which will be forwarded "
               "verbatim for the Clang Static Analyzer. The opion has been "
               "migrated under the clangsa analyzer options: "
               "--analyzer-config clangsa:cc-verbatim-args-file=<filepath>",
       .group = group});
  parser.AddArgument(
      {.flags = {"--tidyargs"},
       .dest = "tidy_args_cfg_file",
       .help = "DEPRECATED. File containing argument which will be forwarded "
               "verbatim for Clang-Tidy. The opion has been migrated under "
               "the clang-tidy analyzer options: --analyzer-config "
               "clang-tidy:cc-verbatim-args-file=<filepath>",
       .group = group});
  parser.AddArgument(
      {.flags = {"--analyzer-config"},
       .dest = "analyzer_config",
       .nargs = argparse::Nargs::kZeroOrMore,
       .type = AnalyzerConfigType,
       .default_list = std::vector<std::string>(),
       .help = "Analyzer configuration options in the following format: "
               "analyzer:key=value. The collection of the options can be "
               "printed with 'codechecker-cpp analyzers --analyzer-config'.\n"
               "If the file at --tidyargs contains a -config flag then those "
               "options extend these.\nTo use an analyzer configuration file "
               "in case of Clang Tidy (.clang-tidy) use the "
               "'clang-tidy:take-config-from-directory=true' option. It will "
               "skip setting the '-checks' parameter of the clang-tidy "
               "binary.",
       .custom_action = OrderedConfigAction,
       .group = group});
  parser.AddArgument(
      {.flags = {"--checker-config"},
       .dest = "checker_config",
       .nargs = argparse::Nargs::kZeroOrMore,
       .type = CheckerConfigType,
       .help = "Checker configuration options in the following format: "
               "analyzer:key=value. The collection of the options can be "
               "printed with 'codechecker-cpp checkers --checker-config'.",
       .custom_action = OrderedConfigAction,
       .group = group});
  parser.AddArgument(
      {.flags = {"--timeout"},
       .dest = "timeout",
       .type = argparse::IntType,
       .help = "The amount of time (in seconds) that each analyzer can spend, "
               "individually, to analyze the project. If the analysis of a "
               "particular file takes longer than this time, the analyzer is "
               "killed and the analysis is considered as a failed one.",
       .group = group});
}

void AddCtuStatisticsAndCheckerArguments(argparse::Parser& parser) {
  ExitIfNoContext();
  const bool stats_capable = IsStatisticsCapable();
  const int ctu_group =
      parser.AddArgumentGroup("cross translation unit analysis arguments",
                              std::string(kCtuDescription));
  const int ctu_modes = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument(
      {.flags = {"--ctu", "--ctu-all"},
       .dest = "ctu_phases",
       .action = argparse::ArgumentAction::kStoreConst,
       .const_value = "11",
       .help = "Perform Cross Translation Unit (CTU) analysis, both 'collect' "
               "and 'analyze' phases. In this mode, the extra files created by "
               "'collect' are cleaned up after the analysis.",
       .group = ctu_group,
       .exclusive_group = ctu_modes});
  parser.AddArgument(
      {.flags = {"--ctu-collect"},
       .dest = "ctu_phases",
       .action = argparse::ArgumentAction::kStoreConst,
       .const_value = "10",
       .help = "Perform the first, 'collect' phase of Cross-TU analysis. This "
               "phase generates extra files needed by CTU analysis, and puts "
               "them into '<OUTPUT_DIR>/ctu-dir'. NOTE: If this argument is "
               "present, CodeChecker will NOT execute the analyzers!",
       .group = ctu_group,
       .exclusive_group = ctu_modes});
  parser.AddArgument(
      {.flags = {"--ctu-analyze"},
       .dest = "ctu_phases",
       .action = argparse::ArgumentAction::kStoreConst,
       .const_value = "01",
       .help = "Perform the second, 'analyze' phase of Cross-TU analysis, "
               "using already available extra files in "
               "'<OUTPUT_DIR>/ctu-dir'. (These files will not be cleaned up "
               "in this mode.)",
       .group = ctu_group,
       .exclusive_group = ctu_modes});
  parser.AddArgument(
      {.flags = {"--ctu-reanalyze-on-failure"},
       .dest = "ctu_reanalyze_on_failure",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "If Cross-TU analysis is enabled and fails for some reason, "
               "try to re analyze the same translation unit without Cross-TU "
               "enabled.",
       .group = ctu_group});
  parser.AddArgument(
      {.flags = {"--ctu-ast-mode"},
       .dest = "ctu_ast_mode",
       .choices = {"load-from-pch", "parse-on-demand"},
       .help = "Choose the way ASTs are loaded during CTU analysis. Mode "
               "'load-from-pch' generates PCH format serialized ASTs during "
               "the 'collect' phase. Mode 'parse-on-demand' only generates the "
               "invocations needed to parse the ASTs. Mode 'load-from-pch' can "
               "use significant disk-space for the serialized ASTs, while mode "
               "'parse-on-demand' can incur some runtime CPU overhead in the "
               "second phase of the analysis. NOTE: Only available if CTU mode "
               "is enabled. (default: parse-on-demand)",
       .group = ctu_group});

  const int stats_group = parser.AddArgumentGroup(
      "Statistics analysis feature arguments", std::string(kStatsDescription));
  parser.AddArgument(
      {.flags = {"--stats-collect", "--stats-collect"},
       .dest = "stats_output",
       .help = "Perform the first, 'collect' phase of Statistical analysis. "
               "This phase generates extra files needed by statistics "
               "analysis, and puts them into '<STATS_OUTPUT>'. NOTE: If this "
               "argument is present, CodeChecker will NOT execute the "
               "analyzers!",
       .group = stats_group});
  parser.AddArgument(
      {.flags = {"--stats-use", "--stats-use"},
       .dest = "stats_dir",
       .help = "Use the previously generated statistics results for the "
               "analysis from the given '<STATS_DIR>'.",
       .group = stats_group});
  parser.AddArgument(
      {.flags = {"--stats"},
       .dest = "stats_enabled",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Perform both phases of Statistical analysis. This phase "
               "generates extra files needed by statistics analysis and "
               "enables the statistical checkers. No need to enable them "
               "explicitly.",
       .group = stats_group});
  parser.AddArgument(
      {.flags = {"--stats-min-sample-count"},
       .dest = "stats_min_sample_count",
       .type = argparse::IntType,
       .default_value =
           stats_capable ? std::optional<std::string>("10") : std::nullopt,
       .help = "Minimum number of samples (function call occurrences) to be "
               "collected for a statistics to be relevant "
               "'<MIN-SAMPLE-COUNT>'.",
       .group = stats_group});
  parser.AddArgument(
      {.flags = {"--stats-relevance-threshold"},
       .dest = "stats_relevance_threshold",
       .type = argparse::FloatType,
       .default_value =
           stats_capable ? std::optional<std::string>("0.85") : std::nullopt,
       .help = "The minimum ratio of calls of function f that must have a "
               "certain property property to consider it true for that "
               "function (calculated as calls with a property/all calls). "
               "CodeChecker will warn for calls of f do not have that "
               "property.'<RELEVANCE_THRESHOLD>'.",
       .group = stats_group});

  const int checkers_group = parser.AddArgumentGroup(
      "checker configuration", std::string(kCheckersDescription));
  parser.AddArgument(
      {.flags = {"-e", "--enable"},
       .dest = "ordered_checkers",
       .metavar = "checker/group/profile",
       .help = "Set a checker (or checker prefix group), profile or guideline "
               "to BE USED" +
               std::string(kEnableHelpTail),
       .custom_action = OrderedCheckersAction("1"),
       .group = checkers_group});
  parser.AddArgument(
      {.flags = {"-d", "--disable"},
       .dest = "ordered_checkers",
       .metavar = "checker/group/profile",
       .help = "Set a checker (or checker prefix group), profile or guideline "
               "to BE PROHIBITED from use" +
               std::string(kEnableHelpTail),
       .custom_action = OrderedCheckersAction("0"),
       .group = checkers_group});
  parser.AddArgument(
      {.flags = {"--enable-all"},
       .dest = "enable_all",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Force the running analyzers to use almost every checker "
               "available. The checker groups 'alpha.', 'debug.','osx.', "
               "'abseil-', 'android-', 'darwin-', 'objc-', "
               "'cppcoreguidelines-', 'fuchsia.', 'fuchsia-', 'hicpp-', "
               "'llvm-', 'llvmlibc-', 'google-', 'zircon-', 'osx.' (on Linux) "
               "are NOT enabled automatically and must be EXPLICITLY "
               "specified. WARNING! Enabling all checkers might result in the "
               "analysis losing precision and stability, and could even "
               "result in a total failure of the analysis. USE WISELY AND AT "
               "YOUR OWN RISK!",
       .group = checkers_group});
  parser.AddArgument(
      {.flags = {"--disable-all"},
       .dest = "disable_all",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Disable all checkers of all analyzers. It is equivalent to "
               "using \"--disable default\" as the first argument.",
       .group = checkers_group});
  parser.AddArgument(
      {.flags = {"--no-missing-checker-error"},
       .dest = "no_missing_checker_error",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Emit a warning instead of an error when an unknown checker "
               "name is given to either --enable, --disable,--analyzer-config "
               "and/or --checker-config.",
       .group = checkers_group});
}

void AddAnalyzeArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));
  parser.set_epilog("\nEnvironment variables\n"
                    "------------------------------------------------\n" +
                    std::string(kAnalyzeEnvironmentVariables) + "\n\n" +
                    std::string(kIssueHashes) + std::string(kEpilogExitStatus));

  parser.AddArgument(
      {.dest = "input",
       .type = ExistingAbspath,
       .help = "The input of the analysis can be either a compilation "
               "database JSON file, a path to a source file or a path to a "
               "directory containing source files."});
  AddJobsArgument(parser, /*group=*/0);
  AddFileFilterArguments(parser);
  AddReviewStatusConfigArgument(parser, /*group=*/0);
  parser.AddArgument(
      {.flags = {"-o", "--output"},
       .dest = "output_path",
       .required = true,
       .help = "Store the analysis output in the given folder."});
  AddCompilerInfoArguments(parser);
  AddOutputFormatArgument(parser);
  parser.AddArgument(
      {.flags = {"--makefile"},
       .dest = "makefile",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help =
           "Generate a Makefile in the given output directory from the "
           "analyzer commands and do not execute the analysis. The "
           "analysis can be executed by calling the make command like "
           "'make -f output_dir/Makefile'. You can ignore errors with the "
           "-i/--ignore-errors options: 'make -f output_dir/Makefile -i'."});
  parser.AddArgument(
      {.flags = {"-q", "--quiet"},
       .dest = "quiet",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Do not print the output or error of the analyzers to the "
               "standard output of CodeChecker."});
  AddCleanArgument(parser, /*group=*/0);
  AddCompileUniqueingArgument(parser);
  AddReportHashArgument(parser, /*group=*/0);
  parser.AddArgument({.flags = {"-n", "--name"},
                      .dest = "name",
                      .help = "Annotate the run analysis with a custom name in "
                              "the created metadata file."});

  const int analyzer_group = parser.AddArgumentGroup("analyzer arguments", "");
  AddAnalyzerArguments(parser, analyzer_group);
  AddZ3RefutationArgument(parser, analyzer_group);
  AddCtuStatisticsAndCheckerArguments(parser);
  common::cli::AddVerboseArgument(parser);
}

Result<int> Analyze(const argparse::Namespace& args) {
  util::SetupLogger(common::cli::Verbose(args), util::Stream::kStdout);
  return AnalyzeMain(ToAnalyzeArgs(args));
}

int RunAnalyze(const argparse::Namespace& args) {
  const Result<int> result = Analyze(args);
  if (!result.ok()) return result.error().exit_code;
  return result.value();
}

}  // namespace cc::analyzer::cli
