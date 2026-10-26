#include "analyzer/drivers/clangtidy.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/context/run_command.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/flag.h"
#include "analyzer/drivers/result_handler.h"
#include "platform/subprocess.h"
#include "report/converter/clang_tidy.h"
#include "report/model/report.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/semver.h"
#include "util/strings.h"
#include "util/yaml.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");
constexpr util::Logger kReportLog("report");
constexpr std::string_view kDiagnosticPrefix = "clang-diagnostic-";

// Python: parse_checkers(): the checker names of `-list-checks`.
std::vector<CheckerInfo> ParseCheckers(std::string_view output) {
  std::vector<CheckerInfo> checkers;
  for (const std::string_view raw : util::StrSplitLines(output)) {
    const std::string_view line = util::Strip(raw);
    if (line.empty() || line.starts_with("Enabled checks:") ||
        line.starts_with("clang-analyzer-")) {
      continue;
    }
    if (util::SplitWhitespace(line).size() == 1) {
      checkers.push_back({.name = std::string(line), .description = ""});
    }
  }
  return checkers;
}

// Returns the diagtool binary next to the clang-tidy binary (Python:
// get_diagtool_bin()), or nullopt.
std::optional<std::string> DiagtoolBinary(const ClangTidy& tidy) {
  std::optional<std::string> binary = tidy.Binary();
  if (!binary) return std::nullopt;
  if (util::pypath::Basename(util::pypath::Realpath(*binary)) == "ccache") {
    // The real clang-tidy is later in PATH than the ccache link.
    const std::vector<std::string> folders =
        util::Split(platform::GetEnv("PATH").value_or(""), ":");
    const std::string link_folder = util::pypath::Dirname(*binary);
    size_t position = 0;
    for (size_t i = 0; i < folders.size(); ++i) {
      if (util::pypath::Normpath(folders[i]) ==
          util::pypath::Normpath(link_folder)) {
        position = i;
        break;
      }
    }
    std::vector<std::string> rest(folders.begin() + std::min(position + 1,
                                                             folders.size()),
                                  folders.end());
    binary = platform::Which(util::pypath::Basename(*binary),
                             util::Join(rest, ":"));
    if (!binary) return std::nullopt;
  }
  const std::string resolved = util::pypath::Realpath(*binary);
  const std::string diagtool =
      util::pypath::Join({util::pypath::Dirname(resolved), "diagtool"});
  if (util::pypath::Exists(diagtool)) return diagtool;
  // Python checks for diagtool-<major> but returns the path of "diagtool".
  if (const std::optional<util::SemVer> version = tidy.Version();
      version && util::pypath::Exists(diagtool + "-" +
                                      std::to_string(version->major))) {
    return diagtool;
  }
  kLog.Warning("'diagtool' can not be found next to the clang binary (" +
               resolved + ")!");
  return std::nullopt;
}

// Python: get_warnings(): the warning flags that diagtool lists, without
// "-W"; nullopt if diagtool fails.
std::optional<std::vector<std::string>> Warnings(const ClangTidy& tidy) {
  const std::optional<std::string> diagtool = DiagtoolBinary(tidy);
  if (!diagtool) return std::vector<std::string>();
  const Result<std::string> output =
      CheckOutput({*diagtool, "tree"}, platform::Output::kInherit);
  if (!output.ok()) {
    if (output.error().message.starts_with("Command")) {
      kLog.Error(
          "'diagtool' encountered an error while retrieving the checker list. "
          "If you are using a custom compiled clang, you may have forgotten "
          "to build the 'diagtool' target alongside 'clang' and "
          "'clang-tidy'! Error message: ");
    }
    return std::nullopt;
  }
  std::vector<std::string> warnings;
  for (const std::string& word : util::SplitWhitespace(output.value())) {
    if (word.starts_with("-W") && word != "-W") {
      warnings.push_back(word.substr(2));
    }
  }
  return warnings;
}

// Returns the proper prefixes of the checker names, split at "-" (for
// Python's _add_asterisk_for_group()).
std::set<std::string> GroupPrefixes(const std::vector<CheckerInfo>& checkers) {
  std::set<std::string> prefixes;
  for (const CheckerInfo& checker : checkers) {
    const std::vector<std::string> parts = util::Split(checker.name, "-");
    std::string prefix;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
      if (i > 0) prefix += "-";
      prefix += parts[i];
      prefixes.insert(prefix);
    }
  }
  return prefixes;
}

// The -checks items and the warning flags for the checker states (Python:
// get_checker_list()).
struct CheckerList {
  std::vector<std::string> checks;
  std::vector<std::string> warnings;
};

// A -config item of Clang-Tidy.
struct ConfigItem {
  std::string key;
  yyjson_mut_val* value;
};

// Sets `key` to `value` in `items`, in place if it is there already, like a
// Python dict.
void SetConfigItem(const std::string& key, yyjson_mut_val* value,
                   std::vector<ConfigItem>* items) {
  const auto it = std::ranges::find(*items, key, &ConfigItem::key);
  if (it != items->end()) {
    it->value = value;
  } else {
    items->push_back({.key = key, .value = value});
  }
}

// Removes the first -config option from the verbatim arguments `extra` and
// sets its JSON items in `items`, copied into `doc`: a -config there wins
// over --analyzer-config. Fails if the JSON is not valid.
Result<void> TakeVerbatimConfig(yyjson_mut_doc* doc,
                                std::vector<std::string>* extra,
                                std::vector<ConfigItem>* items) {
  for (size_t i = 0; i < extra->size(); ++i) {
    const std::string& arg = (*extra)[i];
    if (!arg.starts_with("-config")) continue;
    const bool separate = arg == "-config";
    if (separate && i + 1 >= extra->size()) break;
    const std::string text = separate
                                 ? (*extra)[i + 1]
                                 : arg.substr(std::string("-config=").size());
    const Result<util::JsonDocument> json = util::ParseJson(text);
    if (!json.ok()) return Result<void>(Error{.message = json.error().message});
    yyjson_obj_iter iter =
        yyjson_obj_iter_with(yyjson_doc_get_root(json.value().get()));
    while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
      SetConfigItem(util::JsonToStr(key),
                    yyjson_val_mut_copy(doc, yyjson_obj_iter_get_val(key)),
                    items);
    }
    const auto first = extra->begin() + static_cast<ptrdiff_t>(i);
    extra->erase(first, first + (separate ? 2 : 1));
    break;
  }
  return Result<void>();
}

// Sets in `config` what the -config `items` decide: whether .clang-tidy
// files are used, whether the configuration has checks, and the -config
// JSON, built in `doc`.
void ApplyConfigItems(const std::vector<ConfigItem>& items,
                      yyjson_mut_doc* doc, AnalyzerConfig* config) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  for (const ConfigItem& item : items) {
    yyjson_mut_obj_add(
        object, yyjson_mut_strncpy(doc, item.key.data(), item.key.size()),
        item.value);
    if (item.key == "take-config-from-directory") {
      config->take_config_from_directory =
          util::JsonString(item.value) == "true";
    }
    if (item.key == "Checks") {
      // Python: the truth value of the "Checks" item.
      const util::MutableJsonDocument copy = util::NewJsonDocument();
      yyjson_mut_doc_set_root(copy.get(),
                              yyjson_mut_val_mut_copy(copy.get(), item.value));
      const util::JsonDocument immutable(
          yyjson_mut_doc_imut_copy(copy.get(), nullptr));
      config->checks_in_config =
          util::JsonTruthy(yyjson_doc_get_root(immutable.get()));
    }
  }
  if (!config->take_config_from_directory) {
    config->tidy_config_json = util::DumpJson(object);
  }
}

}  // namespace

std::optional<util::SemVer> ClangTidy::Version() const {
  return BinaryVersion(
      "--version",
      [](const std::string& output) -> std::optional<util::SemVer> {
        static const util::Regex& kVersion =
            *new util::Regex(R"((?s).*version (?P<version>[\d\.]+))");
        const std::optional<util::RegexMatch> match = kVersion.Match(output);
        if (!match) return std::nullopt;
        return util::ParseSemVer(*match->Group("version"));
      },
      kLog);
}

std::optional<std::string> ClangTidy::ResolveMissingBinary(
    const std::string& configured, const Environment& env) const {
  kLog.Debug(configured + " not found in path for ClangTidy!");
  if (configured.starts_with('/')) return std::nullopt;
  std::optional<std::string> tidy = GetBinaryInPath(
      {"clang-tidy"}, R"(^clang-tidy(-\d+(\.\d+){0,2})?$)", env);
  if (tidy) kLog.Debug("Using '" + *tidy + "' for Clang-tidy!");
  return tidy;
}

std::vector<CheckerInfo> ClangTidy::Checkers() const {
  const std::string binary = Binary().value_or("");
  return checkers_.Get(binary, [this, &binary]() -> std::vector<CheckerInfo> {
    const Result<AnalyzerContext*> context = GetContext();
    if (!context.ok()) return {};
    const std::vector<std::string> blacklisted =
        context.value()->checker_labels().CheckersByLabels({"blacklist:true"},
                                                           name());
    const Result<std::string> output =
        CheckOutput({binary, "-list-checks", "-checks=*"},
                    platform::Output::kInherit);
    if (!output.ok()) return {};
    std::vector<CheckerInfo> checkers = ParseCheckers(output.value());
    const std::optional<std::vector<std::string>> warnings = Warnings(*this);
    if (!warnings) return {};
    for (const std::string& warning : *warnings) {
      std::string checker = std::string(kDiagnosticPrefix) + warning;
      if (std::ranges::find(blacklisted, checker) == blacklisted.end()) {
        checkers.push_back({.name = std::move(checker), .description = ""});
      }
    }
    checkers.push_back(
        {.name = "clang-diagnostic-error",
         .description = "Indicates compiler errors."});
    return checkers;
  });
}

std::optional<std::string> ClangTidy::DumpConfig() const {
  const Result<std::string> output =
      CheckOutput({Binary().value_or(""), "-dump-config", "-checks=*"},
                  platform::Output::kInherit);
  if (!output.ok()) return std::nullopt;
  return output.value();
}

std::vector<CheckerOption> ClangTidy::CheckerConfigOptions() const {
  const std::optional<std::string> dump = DumpConfig();
  if (!dump) return {};
  std::vector<std::pair<std::string, std::string>> items;
  // Up to LLVM 14: "key: X value: Y" pairs.
  static const util::Regex& kOldItem =
      *new util::Regex(R"(key:\s+(\S+)\s+value:\s+([^\n]+))");
  for (const util::RegexMatch& match : kOldItem.FindAll(*dump)) {
    items.emplace_back(std::string(*match.Group(1)),
                       std::string(*match.Group(2)));
  }
  util::JsonDocument document;
  if (items.empty()) {
    Result<util::JsonDocument> yaml = util::SafeLoadYaml(*dump, "<dump>");
    if (yaml.ok()) {
      document = std::move(yaml).value();
      yyjson_obj_iter iter = yyjson_obj_iter_with(util::JsonGet(
          yyjson_doc_get_root(document.get()), "CheckOptions"));
      while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
        items.emplace_back(util::JsonToStr(key),
                           util::JsonToStr(yyjson_obj_iter_get_val(key)));
      }
    }
  }
  std::vector<CheckerOption> options;
  for (auto& [key, value] : items) {
    std::ranges::replace(key, '.', ':');
    const size_t colon = key.find(':');
    if (colon == std::string::npos) continue;
    options.push_back({.checker = key.substr(0, colon),
                       .option = key.substr(colon + 1),
                       .documentation = value});
  }
  return options;
}

std::vector<AnalyzerOption> ClangTidy::AnalyzerConfigOptions() const {
  if (!Binary()) return {};
  std::vector<AnalyzerOption> options;
  if (const std::optional<std::string> dump = DumpConfig()) {
    static const util::Regex& kItem =
        *new util::Regex(R"((?m)^(\S+):\s+(\S+)$)");
    for (const util::RegexMatch& match : kItem.FindAll(*dump)) {
      options.push_back({.option = std::string(*match.Group(1)),
                         .documentation = std::string(*match.Group(2))});
    }
  }
  options.push_back(VerbatimArgsOption());
  options.push_back(
      {.option = "take-config-from-directory",
       .documentation = "The .clang-tidy config file should be taken into "
                        "account when analysis is executed through "
                        "CodeChecker. Possible values: true, false. Default: "
                        "false"});
  return options;
}

Result<AnalyzerConfig> ClangTidy::ConstructConfig(
    const ConfigArgs& args) const {
  AnalyzerConfig config;
  config.analyzer = std::string(name());
  config.report_hash = args.report_hash;
  config.add_gcc_include_dirs_with_isystem =
      args.add_gcc_include_dirs_with_isystem;
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  std::vector<ConfigItem> items;
  for (const AnalyzerConfigArg& item : args.analyzer_config) {
    if (item.analyzer == name() && item.option != kVerbatimArgsOption) {
      SetConfigItem(
          item.option,
          yyjson_mut_strncpy(doc, item.value.data(), item.value.size()),
          &items);
    }
  }
  Result<std::vector<std::string>> verbatim =
      LoadVerbatimArgs(name(), args.analyzer_config, kLog);
  if (!verbatim.ok()) return Result<AnalyzerConfig>(verbatim.error());
  config.analyzer_extra_arguments = std::move(verbatim).value();
  // Clang-Tidy hides reports in headers unless told otherwise.
  if (items.empty()) {
    SetConfigItem("HeaderFilterRegex", yyjson_mut_str(doc, ".*"), &items);
  }
  if (Result<void> taken =
          TakeVerbatimConfig(doc, &config.analyzer_extra_arguments, &items);
      !taken.ok()) {
    return Result<AnalyzerConfig>(taken.error());
  }
  if (args.checker_config) {
    yyjson_mut_val* check_options = yyjson_mut_arr(doc);
    for (const CheckerConfigArg& item : *args.checker_config) {
      if (item.analyzer != name()) continue;
      yyjson_mut_val* option = yyjson_mut_arr_add_obj(doc, check_options);
      const std::string key = item.checker + "." + item.option;
      yyjson_mut_obj_add_strncpy(doc, option, "key", key.data(), key.size());
      yyjson_mut_obj_add_strncpy(doc, option, "value", item.value.data(),
                                 item.value.size());
    }
    SetConfigItem("CheckOptions", check_options, &items);
  }
  ApplyConfigItems(items, doc, &config);
  // With .clang-tidy files, compiler warnings are not set from the states.
  std::vector<CheckerInfo> checkers = Checkers();
  if (config.take_config_from_directory) {
    std::erase_if(checkers, [](const CheckerInfo& checker) {
      return checker.name.starts_with(kDiagnosticPrefix);
    });
  }
  if (Result<void> initialized = InitializeCheckers(checkers, args, &config);
      !initialized.ok()) {
    return Result<AnalyzerConfig>(initialized.error());
  }
  return Result<AnalyzerConfig>(std::move(config));
}

std::vector<std::string> ClangTidy::BuildCommand(AnalyzerRun* run) const {
  const AnalyzerConfig& config = *run->config;
  const BuildAction& action = *run->action;
  // Python: get_checker_list().
  CheckerList list;
  std::vector<std::string> enabled;
  for (const CheckerEntry& checker : config.checkers.entries()) {
    const bool on = checker.state == CheckerState::kEnabled;
    if (checker.name.starts_with(kDiagnosticPrefix)) {
      const std::string warning = checker.name.substr(kDiagnosticPrefix.size());
      if (on && checker.name != "clang-diagnostic-error") {
        list.warnings.push_back("-W" + warning);
      } else {
        list.warnings.push_back("-Wno-" + warning);
      }
    }
    if (on) enabled.push_back(checker.name);
  }
  list.checks = {"-*"};
  if (std::ranges::all_of(enabled, [](const std::string& checker) {
        return checker.starts_with(kDiagnosticPrefix);
      })) {
    // A dummy checker, because clang-tidy fails without one.
    list.checks.push_back("darwin-dispatch-once-nonstatic");
  }
  const std::set<std::string>& groups =
      group_prefixes_.Get(Binary().value_or(""), [this] {
        return GroupPrefixes(Checkers());
      });
  for (const std::string& checker : enabled) {
    list.checks.push_back(groups.contains(checker) ? checker + "*" : checker);
  }
  if (config.take_config_from_directory ||
      (!config.tidy_config_json.empty() && config.tidy_config_json != "{}" &&
       config.checks_in_config)) {
    list.checks.clear();
  }

  std::vector<std::string> command = {Binary().value_or("")};
  if (!list.checks.empty()) {
    command.push_back("-checks=" + util::Join(list.checks, ","));
  } else if (const std::optional<util::SemVer> version = Version();
             version && version->major >= 19) {
    // Clang-Tidy 19 fails if no checks are left; they come from the config.
    command.push_back("--allow-no-checks");
  }
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  if (!config.tidy_config_json.empty() && config.tidy_config_json != "{}") {
    command.push_back("-config=" + config.tidy_config_json);
  }
  command.push_back(run->source_file);
  command.insert(command.end(), {"--export-fixes", run->fixit_file});
  command.push_back("--");
  command.push_back("-Qunused-arguments");
  if (!config.enable_all) command.push_back("-Wno-everything");
  if (!HasFlag("-x", command)) {
    command.insert(command.end(), {"-x", action.lang});
  }
  if (!HasFlag("--target", command) && !action.target.empty()) {
    command.push_back("--target=" + action.target);
  }
  if (!HasFlag("-arch", command) && !action.arch.empty()) {
    command.insert(command.end(), {"-arch", action.arch});
  }
  command.insert(command.end(), action.analyzer_options.begin(),
                 action.analyzer_options.end());
  AppendPrependedAll(
      config.add_gcc_include_dirs_with_isystem ? "-isystem" : "-idirafter",
      action.compiler_includes, &command);
  // Python appends the standard even when it is empty.
  if (!HasFlag("-std", command) && !HasFlag("--std", command)) {
    command.push_back(action.compiler_standard);
  }
  if (config.enable_all) {
    command.push_back("-Weverything");
    for (const std::string& warning : list.warnings) {
      if (warning.starts_with("-Wno-")) command.push_back(warning);
    }
  } else {
    command.insert(command.end(), list.warnings.begin(), list.warnings.end());
  }
  return command;
}

std::set<std::string> ClangTidy::MentionedFiles(AnalyzerRun* /*run*/,
                                                std::string_view output) const {
  static const util::Regex& kAnsiEscape = *new util::Regex(R"(\x1b\[[0-9;]*m)");
  static const util::Regex& kLocation = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): )");
  std::set<std::string> paths;
  for (const std::string_view line : util::StrSplitLines(output)) {
    const std::string plain = kAnsiEscape.ReplaceAll(line, "");
    if (const std::optional<util::RegexMatch> match = kLocation.Match(plain)) {
      paths.emplace(*match->Group("path"));
    }
  }
  return paths;
}

Result<void> ClangTidy::PostprocessResult(
    AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
    const common::ReviewStatusHandler* review_status) const {
  kReportLog.DebugAnalyzer(run->out);
  Result<std::vector<report::Report>> parsed =
      report::ParseClangTidyOutput(run->out);
  if (!parsed.ok()) return Result<void>(parsed.error());
  std::vector<report::Report> reports = std::move(parsed).value();
  DropSkippedReports(skip_handlers, &reports);
  SetReportHashes(*run, &reports);
  return WriteResultFiles(*run, std::move(reports), name(), review_status);
}

const ClangTidy& ClangTidyAnalyzer() {
  static const ClangTidy& analyzer = *new ClangTidy;
  return analyzer;
}

}  // namespace cc::analyzer
