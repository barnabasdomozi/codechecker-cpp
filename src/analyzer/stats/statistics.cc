#include "analyzer/stats/statistics.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "analyzer/drivers/clangsa.h"
#include "analyzer/drivers/flag.h"
#include "platform/filesystem.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

// The occurrences of each function in the collector output.
struct FunctionStats {
  int total = 0;
  int unchecked = 0;
  int negative = 0;
  int null = 0;
};

}  // namespace

std::optional<std::vector<std::string>> StatisticsCollectCommand(
    const BuildAction& action, const AnalyzerConfig& config,
    const std::string& source) {
  const ClangSa& clangsa = ClangSaAnalyzer();
  std::vector<std::string> command = {
      clangsa.Binary().value_or(""), "-c", "-x", action.lang, "--analyze",
      "-Qunused-arguments", "--analyzer-output", "text"};
  for (const std::string& plugin : clangsa.Plugins()) {
    command.insert(command.end(), {"-Xclang", "-plugin", "-Xclang",
                                   "checkercfg", "-Xclang", "-load",
                                   "-Xclang", plugin});
  }
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  command.insert(command.end(), action.analyzer_options.begin(),
                 action.analyzer_options.end());
  std::vector<std::string> collectors;
  for (const CheckerInfo& checker :
       clangsa.CheckersWith(/*alpha=*/true, /*debug=*/true)) {
    if (checker.name.find("statisticscollector.SpecialReturnValue") !=
        std::string::npos) {
      collectors.push_back(checker.name);
    }
    if (checker.name.find("statisticscollector.ReturnValueCheck") !=
        std::string::npos) {
      collectors.push_back(checker.name);
    }
  }
  if (collectors.empty()) {
    kLog.Debug("No available statistics collector checkers were found");
    return std::nullopt;
  }
  for (const std::string& collector : collectors) {
    command.insert(command.end(),
                   {"-Xclang", "-analyzer-checker=" + collector});
  }
  if (!HasFlag("--target", command) && !action.target.empty()) {
    command.push_back("--target=" + action.target);
  }
  if (!HasFlag("-std", command) && !HasFlag("--std", command)) {
    command.push_back(action.compiler_standard);
  }
  AppendPrependedAll(
      config.add_gcc_include_dirs_with_isystem ? "-isystem" : "-idirafter",
      action.compiler_includes, &command);
  if (!source.empty()) command.push_back(source);
  return command;
}

std::vector<std::string> StatisticsAnalyzeConfig(
    const std::string& stats_dir) {
  return {"-Xclang", "-analyzer-config", "-Xclang",
          "alpha.ericsson.statisticsbased:APIMetadataPath=" + stats_dir};
}

std::string SpecialReturnStatsFile(const std::string& stats_dir) {
  return util::pypath::Join({stats_dir, "SpecialReturn.yaml"});
}

void PostProcessStats(const std::string& input_dir,
                      const std::string& output_dir, int min_sample_count,
                      double relevance_threshold) {
  if (!util::pypath::Exists(output_dir)) {
    std::error_code error = platform::MakeDirs(output_dir);
    (void)error;
  }
  if (!util::pypath::Exists(input_dir)) return;
  std::set<std::string> lines;
  for (const std::string& name : platform::ListDirectory(input_dir)) {
    const std::string path = util::pypath::Join({input_dir, name});
    if (!util::pypath::IsFile(path)) continue;
    const Result<std::string> content = util::ReadFile(path);
    if (!content.ok()) continue;
    for (const std::string_view line : util::SplitLines(util::TranslateNewlines(
             util::DecodeUtf8Ignore(content.value())))) {
      lines.emplace(line);
    }
  }
  static const util::Regex& kReturnValue = *new util::Regex(
      R"(.*warning: Return Value Check:.*:[0-9]*:[0-9]*.*,(.*),([0,1]))");
  static const util::Regex& kSpecialReturn = *new util::Regex(
      R"(.*warning: Special Return Value:.*:[0-9]*:[0-9]*.*,(.*),([0,1]),([0,1]))");
  std::map<std::string, FunctionStats> unchecked_stats;
  std::map<std::string, FunctionStats> special_stats;
  for (const std::string& line : lines) {
    if (const auto match = kReturnValue.Match(line)) {
      FunctionStats& stats = unchecked_stats[std::string(*match->Group(1))];
      ++stats.total;
      stats.unchecked += *match->Group(2) == "1" ? 1 : 0;
    }
    if (const auto match = kSpecialReturn.Match(line)) {
      FunctionStats& stats = special_stats[std::string(*match->Group(1))];
      ++stats.total;
      stats.negative += *match->Group(2) == "1" ? 1 : 0;
      stats.null += *match->Group(3) == "1" ? 1 : 0;
    }
  }
  const auto relevant = [&](double ratio, const FunctionStats& stats) {
    return relevance_threshold < ratio && ratio < 1 &&
           stats.total >= min_sample_count;
  };
  std::string unchecked = "#\n# UncheckedReturn metadata format 1.0\n";
  for (const auto& [function, stats] : unchecked_stats) {
    const double checked =
        1 - static_cast<double>(stats.unchecked) / stats.total;
    if (relevant(checked, stats)) unchecked += "- " + function + "\n";
  }
  (void)util::WriteFile(
      util::pypath::Join({output_dir, "UncheckedReturn.yaml"}), unchecked);
  std::string special = "#\n# SpecialReturn metadata format 1.0\n";
  for (const auto& [function, stats] : special_stats) {
    if (relevant(static_cast<double>(stats.negative) / stats.total, stats)) {
      special += "{name: " + function + ", relation: LT, value: 0}\n";
    }
  }
  for (const auto& [function, stats] : special_stats) {
    if (relevant(static_cast<double>(stats.null) / stats.total, stats)) {
      special += "{name: " + function + ", relation: EQ, value: 0}\n";
    }
  }
  (void)util::WriteFile(SpecialReturnStatsFile(output_dir), special);
}

}  // namespace cc::analyzer
