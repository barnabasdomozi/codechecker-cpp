#include "analyzer/drivers/analyzer.h"

#include <stddef.h>

#include <algorithm>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/context/run_command.h"
#include "analyzer/drivers/clangsa.h"
#include "analyzer/drivers/clangtidy.h"
#include "analyzer/drivers/cppcheck.h"
#include "analyzer/drivers/gcc.h"
#include "analyzer/drivers/infer.h"
#include "common/checker_labels.h"
#include "common/util.h"
#include "platform/subprocess.h"
#include "util/hash.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("system");

// Checker groups that --enable-all leaves off.
constexpr std::string_view kNotEnabledByAll[] = {
    "alpha.",   "debug.",   "osx.",   "abseil-",  "android-",
    "darwin-",  "objc-",    "cppcoreguidelines-", "fuchsia.",
    "fuchsia-", "hicpp-",   "llvm-",  "llvmlibc-", "google-", "zircon-"};

// Returns whether Python's \w matches the character that starts at `c`.
bool IsWordCharacter(char c) {
  const auto byte = static_cast<unsigned char>(c);
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || byte >= 0x80;
}

// Returns every proper prefix of the checker names, split at "." if the
// name has one, else at "-" (Python: __gen_name_variations(True)).
std::unordered_set<std::string> PrefixGroups(const CheckerTable& table) {
  std::unordered_set<std::string> prefixes;
  for (const CheckerEntry& entry : table.entries()) {
    const std::string_view delimiter =
        entry.name.find('.') != std::string::npos ? "." : "-";
    const std::vector<std::string> parts = util::Split(entry.name, delimiter);
    std::string prefix;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
      if (i > 0) prefix += delimiter;
      prefix += parts[i];
      prefixes.insert(prefix);
    }
  }
  return prefixes;
}

// Python: initialize_checkers_by_namespace().
void ApplyNamespace(const std::string& name_space, const std::string& value,
                    bool enabled, const common::CheckerLabels& labels,
                    CheckerTable* table) {
  if (name_space == "checker") {
    table->SetEnabled(value, enabled, /*strict=*/true);
  } else if (name_space == "prefix") {
    table->SetEnabled(value, enabled, /*strict=*/false);
  } else {
    for (const std::string& checker :
         labels.CheckersByLabels({name_space + ":" + value})) {
      table->SetEnabled(checker, enabled, /*strict=*/true);
    }
  }
}

}  // namespace

void CheckerTable::Add(const std::string& name,
                       const std::string& description) {
  if (const auto it = index_.find(name); it != index_.end()) {
    *it->second = {.name = name,
                   .state = CheckerState::kDisabled,
                   .description = description};
    return;
  }
  entries_.push_back({.name = name,
                      .state = CheckerState::kDisabled,
                      .description = description});
  index_[name] = std::prev(entries_.end());
}

void CheckerTable::SetEnabled(std::string_view name, bool enabled,
                              bool strict) {
  const CheckerState state =
      enabled ? CheckerState::kEnabled : CheckerState::kDisabled;
  std::vector<std::list<CheckerEntry>::iterator> changed;
  if (strict) {
    if (const auto it = index_.find(std::string(name)); it != index_.end()) {
      changed.push_back(it->second);
    }
  } else {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (MatchesCheckerPrefix(name, it->name)) changed.push_back(it);
    }
  }
  // The changed checkers move to the end, so a later, more specific
  // decision wins over an earlier group decision.
  for (const auto it : changed) {
    it->state = state;
    entries_.splice(entries_.end(), entries_, it);
  }
}

const CheckerEntry* CheckerTable::Find(std::string_view name) const {
  const auto it = index_.find(std::string(name));
  return it == index_.end() ? nullptr : &*it->second;
}

bool CheckerTable::HasEnabled() const {
  return std::ranges::any_of(entries_, [](const CheckerEntry& entry) {
    return entry.state == CheckerState::kEnabled;
  });
}

bool MatchesCheckerPrefix(std::string_view name, std::string_view checker) {
  if (!checker.starts_with(name)) return false;
  const bool word_before = !name.empty() && IsWordCharacter(name.back());
  const bool word_after =
      checker.size() > name.size() && IsWordCharacter(checker[name.size()]);
  return word_before != word_after;
}

std::string ActionOutputDir(const AnalyzerRun& run, std::string_view folder) {
  const std::string dir =
      util::pypath::Join({run.workspace, std::string(folder)});
  return run.action_hash.empty() ? dir
                                 : util::pypath::Join({dir, run.action_hash});
}

AnalyzerOption VerbatimArgsOption() {
  return {.option = std::string(kVerbatimArgsOption),
          .documentation = "A file path containing flags that are forwarded "
                           "verbatim to the analyzer tool. E.g.: "
                           "cc-verbatim-args-file=<filepath>",
          .must_exist = true};
}

Result<std::vector<std::string>> LoadVerbatimArgs(
    std::string_view analyzer, const std::vector<AnalyzerConfigArg>& items,
    const util::Logger& log) {
  std::vector<std::string> args;
  for (const AnalyzerConfigArg& item : items) {
    if (item.analyzer != analyzer || item.option != kVerbatimArgsOption) {
      continue;
    }
    Result<std::vector<std::string>> loaded =
        common::LoadArgsFromFile(item.value);
    if (!loaded.ok()) {
      log.Error(loaded.error().message);
      return Result<std::vector<std::string>>(Error{});
    }
    args = std::move(loaded).value();
  }
  return Result<std::vector<std::string>>(std::move(args));
}

Result<std::string> AnalyzerActionHash(const std::string& source,
                                       const std::string& directory,
                                       const std::string& command) {
  Result<std::vector<std::string>> args = util::ShlexSplit(command);
  if (!args.ok()) return Result<std::string>(args.error());
  std::vector<std::string> kept = std::move(args).value();
  for (size_t i = kept.size(); i-- > 0;) {
    if (i >= kept.size() || !kept[i].starts_with("-o")) continue;
    const size_t count = kept[i] == "-o" ? 2 : 1;
    if (i + count > kept.size()) {
      return Result<std::string>(
          Error{.message = "IndexError: list assignment index out of range"});
    }
    kept.erase(kept.begin() + static_cast<ptrdiff_t>(i),
               kept.begin() + static_cast<ptrdiff_t>(i + count));
  }
  const std::string source_path =
      util::pypath::Normpath(util::pypath::Join({directory, source}));
  return Result<std::string>(
      util::Md5Hex(source_path + "_" + util::Join(kept, " ")));
}

Result<AnalyzerRun> MakeAnalyzerRun(const BuildAction& action,
                                    const AnalyzerConfig& config,
                                    const std::string& workspace) {
  Result<std::string> action_hash = AnalyzerActionHash(
      action.source, action.directory, action.original_command);
  if (!action_hash.ok()) return Result<AnalyzerRun>(action_hash.error());
  AnalyzerRun run;
  run.action = &action;
  run.config = &config;
  run.workspace = workspace;
  run.source_file = action.source;
  run.action_hash = std::move(action_hash).value();
  run.action_str = util::pypath::Basename(action.source) + "_" +
                   action.analyzer_type + "_" + run.action_hash;
  run.result_file =
      util::pypath::Join({workspace, run.action_str + ".plist"});
  run.fixit_file =
      util::pypath::Join({workspace, "fixit", run.action_str + ".yaml"});
  return Result<AnalyzerRun>(std::move(run));
}

std::optional<std::string> AnalyzerDriver::Binary() const {
  const Result<AnalyzerContext*> context = GetContext();
  if (!context.ok()) return std::nullopt;
  return context.value()->AnalyzerBinary(name());
}

std::optional<util::SemVer> AnalyzerDriver::BinaryVersion(
    std::string_view flag,
    const std::function<std::optional<util::SemVer>(
        const std::string& output)>& parse,
    const util::Logger& log) const {
  const std::optional<std::string> binary = Binary();
  if (!binary) return std::nullopt;
  return binary_versions_.Get(*binary, [&]() -> std::optional<util::SemVer> {
    const std::vector<std::string> command = {*binary, std::string(flag)};
    const Result<std::string> output =
        CheckOutput(command, platform::Output::kInherit);
    if (!output.ok()) {
      log.Warning("Failed to get analyzer version: " +
                  util::Join(command, " "));
      log.Warning(output.error().message);
      return std::nullopt;
    }
    return parse(output.value());
  });
}

std::optional<std::string> AnalyzerDriver::IncompatibleVersion() const {
  return std::nullopt;
}

std::optional<std::string> AnalyzerDriver::ResolveMissingBinary(
    const std::string& /*configured*/, const Environment& /*env*/) const {
  return std::nullopt;
}

std::vector<CheckerOption> AnalyzerDriver::CheckerConfigOptions() const {
  return {};
}

Environment AnalyzerDriver::CommandEnvironment(
    const std::vector<std::string>& command) const {
  const Result<AnalyzerContext*> context = GetContext();
  return context.ok() ? context.value()->EnvForBin(command[0]) : std::nullopt;
}

void AnalyzerDriver::PostAnalyze(AnalyzerRun* /*run*/) const {}

std::set<std::string> AnalyzerDriver::MentionedFiles(
    AnalyzerRun* /*run*/, std::string_view /*output*/) const {
  return {};
}

Result<void> AnalyzerDriver::InitializeCheckers(
    const std::vector<CheckerInfo>& checkers, const ConfigArgs& args,
    AnalyzerConfig* config) {
  const Result<AnalyzerContext*> context = GetContext();
  if (!context.ok()) return Result<void>(context.error());
  const common::CheckerLabels& labels = context.value()->checker_labels();
  CheckerTable& table = config->checkers;
  for (const CheckerInfo& checker : checkers) {
    table.Add(checker.name, checker.description);
  }

  const std::vector<std::string> default_profile =
      labels.CheckersByLabels({"profile:default"});
  if (default_profile.empty()) kLog.Warning("No default profile found!");
  for (const std::string& checker : default_profile) {
    table.SetEnabled(checker, true, /*strict=*/true);
  }

  config->enable_all = args.enable_all;
  if (args.enable_all) {
    for (const CheckerInfo& checker : checkers) {
      const bool excluded = std::ranges::any_of(
          kNotEnabledByAll, [&checker](std::string_view group) {
            return checker.name.starts_with(group);
          });
      if (!excluded) table.SetEnabled(checker.name, true, /*strict=*/false);
    }
  }

  // The namespaces of -e/-d items: checker names, prefix groups and labels.
  std::vector<std::string> namespaces = {"checker", "prefix"};
  namespaces.insert(namespaces.end(), labels.Labels().begin(),
                    labels.Labels().end());
  const std::unordered_set<std::string> prefixes = PrefixGroups(table);
  const auto occurs = [&](const std::string& name_space,
                          const std::string& value) {
    if (name_space == "checker") return table.Find(value) != nullptr;
    if (name_space == "prefix") return prefixes.contains(value);
    const std::vector<std::string>& values = labels.OccurringValues(name_space);
    return std::ranges::find(values, value) != values.end();
  };
  for (const auto& [identifier, enabled] : args.ordered_checkers) {
    const size_t colon = identifier.find(':');
    if (colon != std::string::npos) {
      const std::string name_space = identifier.substr(0, colon);
      const std::string value = identifier.substr(colon + 1);
      if (std::ranges::find(namespaces, name_space) == namespaces.end()) {
        kLog.Error(std::format(
            "The {} namespace is not known. Please selectone of these "
            "existing namespace options: {}.",
            name_space, util::Join(namespaces, ", ")));
        return Result<void>(Error{});
      }
      if (name_space != "checker" && name_space != "prefix" &&
          !occurs(name_space, value)) {
        kLog.Error(std::format(
            "The {} identifier does not exist in the {} namespace. Please "
            "select one of these existing options: {}.",
            value, name_space,
            util::Join(labels.OccurringValues(name_space), ", ")));
        return Result<void>(Error{});
      }
      ApplyNamespace(name_space, value, enabled, labels, &table);
      continue;
    }
    // Python: the label namespaces first, then prefix and checker.
    std::vector<std::string> possible;
    for (size_t i = 2; i < namespaces.size(); ++i) {
      if (occurs(namespaces[i], identifier)) possible.push_back(namespaces[i]);
    }
    for (const char* name_space : {"prefix", "checker"}) {
      if (occurs(name_space, identifier)) possible.emplace_back(name_space);
    }
    if (possible.size() == 1) {
      ApplyNamespace(possible.front(), identifier, enabled, labels, &table);
    } else if (possible.size() > 1) {
      std::vector<std::string> options;
      for (const std::string& name_space : possible) {
        options.push_back(name_space + ":" + identifier);
      }
      kLog.Error(std::format(
          "The {} is ambiguous. Please select one of these options to "
          "clarify the checker list: {}.",
          identifier, util::Join(options, ", ")));
      return Result<void>(Error{});
    }
    // An unknown name is reported later, by the analyze command.
  }
  return Result<void>();
}

std::span<const AnalyzerDriver* const> SupportedAnalyzers() {
  static const AnalyzerDriver* const kAnalyzers[] = {
      &ClangSaAnalyzer(), &ClangTidyAnalyzer(), &CppcheckAnalyzer(),
      &GccAnalyzer(), &InferAnalyzer()};
  return kAnalyzers;
}

const AnalyzerDriver* FindAnalyzer(std::string_view name) {
  for (const AnalyzerDriver* analyzer : SupportedAnalyzers()) {
    if (analyzer->name() == name) return analyzer;
  }
  return nullptr;
}

}  // namespace cc::analyzer
