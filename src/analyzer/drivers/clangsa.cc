#include "analyzer/drivers/clangsa.h"

#include <yyjson.h>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/context/run_command.h"
#include "analyzer/ctu/ctu_triple_arch.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/flag.h"
#include "analyzer/drivers/result_handler.h"
#include "analyzer/host_check.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "report/parser/plist.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/plist.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/semver.h"
#include "util/strings.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

// The statistics checkers (Python: SpecialReturnValueCollector and
// ReturnValueCollector).
constexpr std::string_view kSpecialReturnValueCollect =
    "statisticscollector.SpecialReturnValue";
constexpr std::string_view kReturnValueCollect =
    "statisticscollector.ReturnValueCheck";
constexpr std::string_view kSpecialReturnValueAnalyze =
    "statisticsbased.SpecialReturnValue";
constexpr std::string_view kReturnValueAnalyze =
    "statisticsbased.UncheckedReturnValue";

// Returns whether the user disabled `checker` with a -d item that is its
// prefix (Python: _is_user_disabled_checker()).
bool IsUserDisabledChecker(
    const std::string& checker,
    const std::vector<CheckerChoice>& ordered_checkers) {
  return std::ranges::any_of(
      ordered_checkers, [&checker](const CheckerChoice& item) {
        return !item.enabled && checker.starts_with(item.name);
      });
}

// Returns the first checker whose name contains `part`, or nullopt.
std::optional<std::string> FindChecker(const std::vector<CheckerInfo>& checkers,
                                       std::string_view part) {
  for (const CheckerInfo& checker : checkers) {
    if (checker.name.find(part) != std::string::npos) return checker.name;
  }
  return std::nullopt;
}

// Python: analyzer_mentioned_file_real_path(): the source file of an AST
// file that Clang loaded for CTU.
std::string MentionedFileRealPath(const ClangSa& clangsa, AnalyzerRun* run,
                                  const std::string& mentioned) {
  const std::string ctu_dir = clangsa.CtuDir(run);
  const std::string path = util::pypath::Join({ctu_dir, mentioned});
  const size_t suffix = path.rfind(".ast");
  if (suffix == std::string::npos) {
    kLog.Debug(std::format("Analyzer mentioned path path: '{}', corresponding "
                           "source file: '{}'",
                           path, path));
    return path;
  }
  const std::string ast_dir = util::pypath::Join({ctu_dir, "ast"});
  const std::string source =
      suffix >= ast_dir.size()
          ? path.substr(ast_dir.size(), suffix - ast_dir.size())
          : "";
  kLog.Debug(std::format("Analyzer mentioned path path: '{}', corresponding "
                         "source file: '{}'",
                         path, source));
  if (!path.starts_with(ast_dir)) {
    kLog.Error(std::format("Mentioned path '{}' ends with suffix '.ast', but "
                           "does not begin with supposed ast dir '{}'.",
                           path, ast_dir));
  }
  return source;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> ParseClangHelpPage(
    const std::vector<std::string>& command, std::string_view start_label) {
  std::vector<std::pair<std::string, std::string>> result;
  const Result<std::string> output =
      CheckOutput(command, platform::Output::kStdout);
  if (!output.ok()) {
    kLog.Debug("Failed to run '" + util::Join(command, " ") + "' command!");
    return result;
  }
  const size_t label = output.value().find(start_label);
  if (label == std::string::npos) return result;
  static const util::Regex& kNewOption =
      *new util::Regex(R"(^\s{2}(?P<flag>\S+)(\s(?P<desc>[^\n]+))?$)");
  static const util::Regex& kDescription =
      *new util::Regex(R"(^\s{3,}(?P<desc>[^\n]+)$)");
  std::optional<std::string> flag;
  std::vector<std::string> description;
  const std::string_view help =
      std::string_view(output.value()).substr(label + start_label.size());
  for (const std::string_view line : util::StrSplitLines(help)) {
    std::optional<util::RegexMatch> match = kNewOption.Match(line);
    if (match) {
      if (flag && !description.empty()) {
        result.emplace_back(*flag, util::Join(description, " "));
        flag.reset();
        description.clear();
      }
      flag = std::string(*match->Group("flag"));
    } else {
      match = kDescription.Match(line);
    }
    if (match) {
      if (const std::optional<std::string_view> text = match->Group("desc");
          text && !text->empty()) {
        description.emplace_back(util::Strip(*text));
      }
    }
  }
  if (flag && !description.empty()) {
    result.emplace_back(*flag, util::Join(description, " "));
  }
  return result;
}

std::optional<util::SemVer> ClangSa::Version() const {
  return BinaryVersion(
      "-dumpversion",
      [](const std::string& output) {
        std::string text(util::Strip(output));
        if (text.ends_with("git")) text.resize(text.size() - 3);
        return util::ParseSemVer(text);
      },
      kLog);
}

std::optional<std::string> ClangSa::ResolveMissingBinary(
    const std::string& configured, const Environment& env) const {
  kLog.Debug(configured + " not found in path for ClangSA!");
  if (configured.starts_with('/')) return std::nullopt;
  std::optional<std::string> clang = GetBinaryInPath(
      {"clang", "clang++"}, R"(^clang(\+\+)?(-\d+(\.\d+){0,2})?$)", env);
  if (clang) kLog.Debug("Using '" + *clang + "' for ClangSA!");
  return clang;
}

std::vector<std::string> ClangSa::Plugins() const {
  const Result<AnalyzerContext*> context = GetContext();
  std::optional<std::string> plugin_dir =
      context.ok() ? context.value()->CheckerPluginDir() : std::nullopt;
  if (IsAnalyzerFromPath()) {
    plugin_dir = platform::GetEnv("CC_CLANGSA_PLUGIN_DIR");
    if (!plugin_dir || plugin_dir->empty()) return {};
  }
  if (!plugin_dir || plugin_dir->empty() ||
      !util::pypath::Exists(*plugin_dir)) {
    return {};
  }
  std::vector<std::string> plugins;
  for (const std::string& name : platform::ListDirectory(*plugin_dir)) {
    const std::string path = util::pypath::Join({*plugin_dir, name});
    if (util::pypath::IsFile(path) && name.ends_with(".so")) {
      plugins.push_back(path);
    }
  }
  return plugins;
}

std::vector<std::string> ClangSa::PluginLoadFlags() const {
  std::vector<std::string> flags;
  for (const std::string& plugin : Plugins()) {
    flags.push_back("-load");
    flags.push_back(plugin);
  }
  return flags;
}

std::string ClangSa::Cc1Help() const {
  const std::string binary = Binary().value_or("");
  return cc1_help_.Get(binary, [&binary] {
    const Result<std::string> help =
        CheckOutput({binary, "-cc1", "--help"}, platform::Output::kStdout);
    return help.ok() ? help.value() : std::string();
  });
}

std::vector<CheckerInfo> ClangSa::Checkers() const {
  return CheckersWith(/*alpha=*/true, /*debug=*/false);
}

std::vector<CheckerInfo> ClangSa::CheckersWith(bool alpha, bool debug) const {
  const std::string binary = Binary().value_or("");
  return checkers_.Get({binary, alpha, debug}, [&] {
    std::vector<std::string> command = {binary, "-cc1"};
    const std::vector<std::string> plugins = PluginLoadFlags();
    command.insert(command.end(), plugins.begin(), plugins.end());
    command.push_back("-analyzer-checker-help");
    if (Version()) {
      const std::string help = Cc1Help();
      if (alpha && help.find("-analyzer-checker-help-alpha") !=
                       std::string::npos) {
        command.push_back("-analyzer-checker-help-alpha");
      }
      if (debug && help.find("-analyzer-checker-help-developer") !=
                       std::string::npos) {
        command.push_back("-analyzer-checker-help-developer");
      }
    }
    std::vector<CheckerInfo> checkers;
    for (auto& [checker, description] :
         ParseClangHelpPage(command, "CHECKERS:")) {
      checkers.push_back({.name = std::move(checker),
                          .description = std::move(description)});
    }
    return checkers;
  });
}

std::vector<CheckerOption> ClangSa::CheckerConfigOptions() const {
  std::vector<std::string> command = {Binary().value_or(""), "-cc1"};
  const std::vector<std::string> plugins = PluginLoadFlags();
  command.insert(command.end(), plugins.begin(), plugins.end());
  command.push_back("-analyzer-checker-option-help");
  if (Version()) {
    const std::string help = Cc1Help();
    for (const char* flag : {"-analyzer-checker-option-help-alpha",
                             "-analyzer-checker-option-help-developer"}) {
      if (help.find(flag) != std::string::npos) command.push_back(flag);
    }
  }
  std::vector<CheckerOption> options;
  for (const auto& [config, documentation] :
       ParseClangHelpPage(command, "OPTIONS:")) {
    const size_t colon = config.find(':');
    if (colon == std::string::npos) continue;
    options.push_back({.checker = config.substr(0, colon),
                       .option = config.substr(colon + 1),
                       .documentation = documentation});
  }
  return options;
}

std::vector<AnalyzerOption> ClangSa::AnalyzerConfigOptions() const {
  std::vector<std::string> command = {Binary().value_or(""), "-cc1"};
  const std::vector<std::string> plugins = PluginLoadFlags();
  command.insert(command.end(), plugins.begin(), plugins.end());
  command.push_back("-analyzer-config-help");
  std::vector<AnalyzerOption> options;
  for (const auto& [option, documentation] :
       ParseClangHelpPage(command, "OPTIONS:")) {
    options.push_back({.option = option, .documentation = documentation});
  }
  options.push_back(VerbatimArgsOption());
  return options;
}

std::optional<CtuMappingTool> ClangSa::CtuMapping() const {
  const std::optional<util::SemVer> version = Version();
  if (!version) {
    kLog.Debug("No clang version information. Can not detect ctu mapping "
               "tool.");
    return std::nullopt;
  }
  const bool new_tool = version->major > 7;
  const std::string tool_name =
      new_tool ? "clang-extdef-mapping" : "clang-func-mapping";
  const std::string map_file =
      new_tool ? "externalDefMap.txt" : "externalFnMap.txt";
  const std::string binary = Binary().value_or("");
  const std::string installed_dir = util::pypath::Dirname(binary);
  const std::string tool = util::pypath::Join({installed_dir, tool_name});
  if (util::pypath::IsFile(tool)) {
    return CtuMappingTool{.tool = tool, .map_file = map_file};
  }
  kLog.Debug(std::format(
      "Mapping tool '{}' suggested by autodetection is not found in directory "
      "reported by Clang '{}'. Trying with version-postfixed filename...",
      tool, installed_dir));
  const std::string postfixed = tool + "-" + std::to_string(version->major);
  if (util::pypath::IsFile(postfixed)) {
    return CtuMappingTool{.tool = postfixed, .map_file = map_file};
  }
  kLog.Debug(std::format(
      "Postfixed mapping tool '{}' suggested by autodetection is not found in "
      "directory reported by Clang '{}'.",
      postfixed, installed_dir));
  return std::nullopt;
}

bool ClangSa::IsCtuCapable() const {
  const std::optional<CtuMappingTool> mapping = CtuMapping();
  if (!mapping) return false;
  const Result<std::string> version =
      CheckOutput({mapping->tool, "-version"}, platform::Output::kStdout);
  return version.ok();
}

bool ClangSa::IsOnDemandCtuAvailable() const {
  const Result<std::string> options =
      CheckOutput({Binary().value_or(""), "-cc1", "-analyzer-config-help"},
                  platform::Output::kStdout);
  return options.ok() &&
         options.value().find("ctu-invocation-list") != std::string::npos;
}

std::vector<std::string> ClangSa::DisplayProgressFlags() const {
  const std::string binary = Binary().value_or("");
  const bool available = display_progress_.Get(binary, [&binary] {
    return HasAnalyzerConfigOption(binary, "display-ctu-progress");
  });
  if (!available) return {};
  return {"-Xclang", "-analyzer-config", "-Xclang",
          "display-ctu-progress=true"};
}

std::string ClangSa::CtuDir(AnalyzerRun* run) const {
  if (!run->ctu_dir) {
    run->ctu_dir = util::pypath::Join(
        {run->config->ctu_dir,
         TripleArch(*run->action, run->source_file, *run->config)});
  }
  return *run->ctu_dir;
}

Result<AnalyzerConfig> ClangSa::ConstructConfig(const ConfigArgs& args) const {
  AnalyzerConfig config;
  config.analyzer = std::string(name());
  config.report_hash = args.report_hash;
  config.enable_z3_refutation = args.enable_z3_refutation;
  config.add_gcc_include_dirs_with_isystem =
      args.add_gcc_include_dirs_with_isystem;
  config.ctu_dir = args.ctu_dir;
  config.ctu_on_demand = args.ctu_on_demand;
  const std::vector<CheckerInfo> checkers = Checkers();
  if (Result<void> initialized = InitializeCheckers(checkers, args, &config);
      !initialized.ok()) {
    return Result<AnalyzerConfig>(initialized.error());
  }
  // --stats turns on the statistics checkers, unless they are disabled.
  if (args.stats_enabled) {
    for (const std::string_view checker :
         {kSpecialReturnValueAnalyze, kReturnValueAnalyze}) {
      const std::optional<std::string> found = FindChecker(checkers, checker);
      if (found && !IsUserDisabledChecker(*found, args.ordered_checkers)) {
        config.checkers.SetEnabled(*found, true, /*strict=*/false);
      }
    }
  }
  // The collector checkers ruin the output; they run in the pre-analysis.
  config.checkers.SetEnabled(kSpecialReturnValueCollect, false,
                             /*strict=*/false);
  config.checkers.SetEnabled(kReturnValueCollect, false, /*strict=*/false);
  if (args.checker_config) {
    for (const CheckerConfigArg& item : *args.checker_config) {
      if (item.analyzer != name()) continue;
      config.checker_config_options.push_back(
          std::format("{}:{}={}", item.checker, item.option, item.value));
    }
  }
  for (const AnalyzerConfigArg& item : args.analyzer_config) {
    if (item.analyzer == name() && item.option != kVerbatimArgsOption) {
      config.checker_config_options.push_back(item.option + "=" + item.value);
    }
  }
  Result<std::vector<std::string>> extra =
      LoadVerbatimArgs(name(), args.analyzer_config, kLog);
  if (!extra.ok()) return Result<AnalyzerConfig>(extra.error());
  config.analyzer_extra_arguments = std::move(extra).value();
  return Result<AnalyzerConfig>(std::move(config));
}

std::vector<std::string> ClangSa::BuildCommand(AnalyzerRun* run) const {
  const AnalyzerConfig& config = *run->config;
  const BuildAction& action = *run->action;
  std::vector<std::string> command = {Binary().value_or(""), "--analyze",
                                      "-Qunused-arguments"};
  for (const std::string& plugin : Plugins()) {
    command.insert(command.end(), {"-Xclang", "-plugin", "-Xclang",
                                   "checkercfg", "-Xclang", "-load",
                                   "-Xclang", plugin});
  }
  command.insert(command.end(), {"-Xclang", "-analyzer-output=plist-multi-file",
                                 "-o", run->result_file});
  command.insert(command.end(), {"-Xclang", "-analyzer-config", "-Xclang",
                                 "expand-macros=true"});
  // Checker options must come before the checkers.
  for (const std::vector<std::string>& checker_config : run->checker_configs) {
    command.insert(command.end(), checker_config.begin(), checker_config.end());
  }
  for (const std::string& option : config.checker_config_options) {
    command.insert(command.end(),
                   {"-Xclang", "-analyzer-config", "-Xclang", option});
  }
  run->disabled_checkers.clear();
  std::vector<std::string> enabled;
  for (const CheckerEntry& checker : config.checkers.entries()) {
    if (checker.state == CheckerState::kEnabled) {
      enabled.push_back(checker.name);
    } else {
      run->disabled_checkers.push_back(checker.name);
    }
  }
  if (!enabled.empty()) {
    command.insert(command.end(), {"-Xclang", "-analyzer-checker=" +
                                                  util::Join(enabled, ",")});
  }
  if (const std::optional<util::SemVer> version = Version();
      version && version->major >= 8) {
    command.insert(command.end(),
                   {"-Xclang", "-analyzer-config", "-Xclang",
                    "aggressive-binary-operation-simplification=true"});
  }
  if (config.enable_z3_refutation) {
    command.insert(command.end(), {"-Xclang", "-analyzer-config", "-Xclang",
                                   "crosscheck-with-z3=true"});
  }
  if (!config.ctu_dir.empty() && !run->disable_ctu) {
    const std::string ctu_dir = CtuDir(run);
    command.insert(command.end(),
                   {"-Xclang", "-analyzer-config", "-Xclang",
                    "experimental-enable-naive-ctu-analysis=true", "-Xclang",
                    "-analyzer-config", "-Xclang", "ctu-dir=" + ctu_dir});
    const std::vector<std::string> progress = DisplayProgressFlags();
    command.insert(command.end(), progress.begin(), progress.end());
    if (config.ctu_on_demand) {
      command.insert(
          command.end(),
          {"-Xclang", "-analyzer-config", "-Xclang",
           "ctu-invocation-list=" +
               util::pypath::Join({ctu_dir, "invocation-list.yml"})});
    }
  }
  if (!HasFlag("-x", command)) {
    command.insert(command.end(), {"-x", action.lang});
  }
  if (!HasFlag("-arch", command) && !action.arch.empty()) {
    command.insert(command.end(), {"-arch", action.arch});
  }
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  command.insert(command.end(), action.analyzer_options.begin(),
                 action.analyzer_options.end());
  if (!HasFlag("--target", command) && !HasFlag("-target", command) &&
      !action.target.empty()) {
    command.push_back("--target=" + action.target);
  }
  if (!HasFlag("-std", command) && !HasFlag("--std", command) &&
      !action.compiler_standard.empty()) {
    command.push_back(action.compiler_standard);
  }
  AppendPrependedAll(
      config.add_gcc_include_dirs_with_isystem ? "-isystem" : "-idirafter",
      action.compiler_includes, &command);
  command.push_back(run->source_file);
  return command;
}

void ClangSa::PostAnalyze(AnalyzerRun* run) const {
  // Disabled checkers run anyway, because others may depend on them; their
  // reports are removed here.
  if (!util::pypath::IsFile(run->result_file)) return;
  const Result<std::string> content = util::ReadFile(run->result_file);
  if (!content.ok()) return;
  // A file written by two parallel runs of the same action may be invalid.
  const Result<util::JsonDocument> plist =
      util::ParsePlist(content.value(), run->result_file);
  if (!plist.ok()) return;
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* root =
      yyjson_val_mut_copy(doc, yyjson_doc_get_root(plist.value().get()));
  if (!yyjson_mut_is_obj(root)) return;
  yyjson_mut_val* kept = yyjson_mut_arr(doc);
  yyjson_mut_val* diagnostics = yyjson_mut_obj_get(root, "diagnostics");
  yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(diagnostics);
  while (yyjson_mut_val* diagnostic = yyjson_mut_arr_iter_next(&iter)) {
    yyjson_mut_val* check_name = yyjson_mut_obj_get(diagnostic, "check_name");
    const std::string checker = util::JsonString(check_name);
    if (std::ranges::find(run->disabled_checkers, checker) ==
        run->disabled_checkers.end()) {
      yyjson_mut_arr_append(kept, yyjson_mut_val_mut_copy(doc, diagnostic));
    }
  }
  yyjson_mut_obj_put(root, yyjson_mut_str(doc, "diagnostics"), kept);
  const Result<std::string> text = util::DumpPlist(root);
  if (text.ok()) (void)util::WriteFile(run->result_file, text.value());
}

std::set<std::string> ClangSa::MentionedFiles(AnalyzerRun* run,
                                              std::string_view output) const {
  std::set<std::string> paths;
  static const util::Regex& kCtuLoad =
      *new util::Regex("CTU loaded AST file: (.*)");
  for (const std::string_view line : util::StrSplitLines(output)) {
    if (const std::optional<util::RegexMatch> match = kCtuLoad.Match(line)) {
      paths.insert(
          MentionedFileRealPath(*this, run, std::string(*match->Group(1))));
    }
  }
  return paths;
}

Result<void> ClangSa::PostprocessResult(
    AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
    const common::ReviewStatusHandler* review_status) const {
  if (Result<void> updated = UpdateErrorFile(*run, name()); !updated.ok()) {
    return updated;
  }
  if (!util::pypath::Exists(run->result_file)) return Result<void>();
  std::vector<report::Report> reports =
      report::ReadPlistFile(run->result_file, SourceDirPath(*run));
  DropSkippedReports(skip_handlers, &reports);
  const std::optional<std::string>& hash = run->config->report_hash;
  std::optional<report::HashType> hash_type;
  if (hash == "context-free" || hash == "context-free-v2") {
    hash_type = report::HashType::kContextFree;
  } else if (hash == "diagnostic-message") {
    hash_type = report::HashType::kDiagnosticMessage;
  }
  if (hash_type) {
    report::SourceFileCache sources;
    for (report::Report& report : reports) {
      report.report_hash = report::GetReportHash(report, *hash_type, sources);
    }
  }
  return WriteReports(*run, std::move(reports), name(), review_status);
}

const ClangSa& ClangSaAnalyzer() {
  static const ClangSa& analyzer = *new ClangSa;
  return analyzer;
}

}  // namespace cc::analyzer
