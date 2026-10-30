#include "analyzer/drivers/analyzer_types.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/clangsa.h"
#include "analyzer/host_check.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

void AddFailure(std::vector<std::pair<std::string, std::string>>* failed,
                const std::string& name, const std::string& reason) {
  const std::pair<std::string, std::string> item(name, reason);
  if (std::ranges::find(*failed, item) == failed->end()) {
    failed->push_back(item);
  }
}

}  // namespace

AnalyzerAvailability CheckSupportedAnalyzers(
    const std::vector<std::string>& names) {
  AnalyzerAvailability result;
  const Result<AnalyzerContext*> context = GetContext();
  for (const std::string& name : names) {
    const AnalyzerDriver* analyzer = FindAnalyzer(name);
    if (analyzer == nullptr) {
      AddFailure(&result.failed, name, "Analyzer unsupported by CodeChecker!");
      continue;
    }
    std::optional<std::string> binary =
        context.ok() ? context.value()->AnalyzerBinary(name) : std::nullopt;
    if (!binary || binary->empty()) {
      AddFailure(&result.failed, name, "Failed to detect analyzer binary!");
      continue;
    }
    if (!binary->starts_with('/')) {
      // A binary without an absolute path is looked up in PATH.
      const std::optional<std::string> found = analyzer->ResolveMissingBinary(
          *binary, context.value()->EnvForBin(*binary));
      if (found && util::pypath::Basename(*found) != *binary) {
        kLog.Debug("Configured binary '" + *binary + "' for analyzer '" +
                   name + "' was not found, but environment PATH contains '" +
                   *found + "'.");
        context.value()->SetAnalyzerBinary(name,
                                           util::pypath::Realpath(*found));
      }
      binary = found;
    }
    bool available = true;
    if (binary) {
      if (const std::optional<std::string> error =
              analyzer->IncompatibleVersion()) {
        AddFailure(&result.failed, name,
                   "Incompatible version: " + *error +
                       " Maybe try setting an absolute path to a different "
                       "analyzer binary via the env variable "
                       "CC_ANALYZER_BIN?");
        available = false;
      }
    }
    if (!binary || !CheckAnalyzer(*binary)) {
      AddFailure(&result.failed, name, "Cannot execute analyzer binary!");
      available = false;
    }
    if (available &&
        std::ranges::find(result.enabled, name) == result.enabled.end()) {
      result.enabled.push_back(name);
    }
  }
  return result;
}

Result<AnalyzerAvailability> CheckAvailableAnalyzers(
    const std::optional<std::vector<std::string>>& names) {
  if (names && !names->empty()) {
    AnalyzerAvailability result = CheckSupportedAnalyzers(*names);
    if (!result.failed.empty()) {
      PrintUnsupportedAnalyzers(result.failed);
      kLog.Error("Failed to run command because the given analyzer(s) cannot "
                 "be found on your machine!");
      return Result<AnalyzerAvailability>(Error{});
    }
    return Result<AnalyzerAvailability>(std::move(result));
  }
  AnalyzerAvailability result =
      CheckSupportedAnalyzers(SupportedAnalyzerNames());
  if (result.enabled.empty()) {
    PrintUnsupportedAnalyzers(result.failed);
    kLog.Error("Failed to run command because no analyzers can be found on "
               "your machine!");
    return Result<AnalyzerAvailability>(Error{});
  }
  return Result<AnalyzerAvailability>(std::move(result));
}

void PrintUnsupportedAnalyzers(
    const std::vector<std::pair<std::string, std::string>>& failed) {
  for (const auto& [name, reason] : failed) {
    kLog.Warning("Analyzer '" + name +
                 "' is enabled but CodeChecker failed to execute analysis "
                 "with it: '" +
                 reason +
                 "'. Please check your 'PATH' environment variable and the "
                 "'config/package_layout.json' file!");
  }
}

bool IsStatisticsCapable() {
  if (CheckSupportedAnalyzers({"clangsa"}).enabled.empty()) return false;
  static const util::Regex& kCollector =
      *new util::Regex(".+statisticscollector.+");
  return std::ranges::any_of(
      ClangSaAnalyzer().CheckersWith(/*alpha=*/true, /*debug=*/true),
      [](const CheckerInfo& checker) {
        return kCollector.Match(checker.name).has_value();
      });
}

bool IsZ3Capable() {
  if (CheckSupportedAnalyzers({"clangsa"}).enabled.empty()) return false;
  const std::string clang = ClangSaAnalyzer().Binary().value_or("");
  // Clang 23 renamed -analyzer-constraints=z3 to unsupported-z3.
  return HasAnalyzerOption(
             clang, {"-Xclang", "-analyzer-constraints=unsupported-z3"}) ||
         HasAnalyzerOption(clang, {"-Xclang", "-analyzer-constraints=z3"});
}

bool IsZ3RefutationCapable() {
  if (!IsZ3Capable()) return false;
  CheckSupportedAnalyzers({"clangsa"});
  return HasAnalyzerConfigOption(ClangSaAnalyzer().Binary().value_or(""),
                                 "crosscheck-with-z3");
}

Result<std::map<std::string, AnalyzerConfig>> BuildConfigs(
    const ConfigArgs& args, const std::vector<std::string>& analyzers) {
  std::map<std::string, AnalyzerConfig> configs;
  for (const std::string& name : analyzers) {
    Result<AnalyzerConfig> config = FindAnalyzer(name)->ConstructConfig(args);
    if (!config.ok()) {
      return Result<std::map<std::string, AnalyzerConfig>>(config.error());
    }
    configs.emplace(name, std::move(config).value());
  }
  return Result<std::map<std::string, AnalyzerConfig>>(std::move(configs));
}

}  // namespace cc::analyzer
