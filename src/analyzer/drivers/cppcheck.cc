#include "analyzer/drivers/cppcheck.h"

#include <stddef.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/context/run_command.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/result_handler.h"
#include "platform/filesystem.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "report/parser/plist.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/semver.h"
#include "util/strings.h"
#include "util/xml.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer.cppcheck");

// The standards Cppcheck does not know, and what to use instead.
constexpr std::pair<std::string_view, std::string_view> kStandards[] = {
    {"c90", "c89"},          {"c18", "c17"},          {"iso9899:2017", "c17"},
    {"iso9899:2018", "c17"}, {"iso9899:1990", "c89"}, {"iso9899:199409", "c89"},
    {"c9x", "c99"},          {"iso9899:1999", "c99"}, {"iso9899:199x", "c99"},
    {"c1x", "c11"},          {"iso9899:2011", "c11"}, {"c2x", "c23"},
    {"iso9899:2024", "c23"}, {"c++98", "c++03"},      {"c++0x", "c++11"},
    {"c++1y", "c++14"},      {"c++1z", "c++17"},      {"c++2a", "c++20"},
    {"c++2b", "c++23"},      {"c++2c", "c++26"}};

// Python: parse_analyzer_config(): the compiler flags Cppcheck understands.
std::vector<std::string> WhitelistedFlags(const BuildAction& action) {
  static const util::Regex& kInteresting = *new util::Regex("-[IUD].*");
  static const util::Regex& kStd = *new util::Regex("-(-std$|-?std=.*)");
  const std::vector<std::string>& options = action.analyzer_options;
  std::vector<std::string> params;
  for (size_t i = 0; i < options.size(); ++i) {
    if (kInteresting.Match(options[i])) {
      params.push_back(options[i]);
      // "-I <path>": the value is the next argument.
      if (options[i].size() == 2 && i + 1 < options.size()) {
        params.push_back(options[i + 1]);
      }
    } else if (kStd.Match(options[i])) {
      std::string standard;
      if (options[i].find('=') != std::string::npos) {
        standard = options[i].substr(options[i].rfind('=') + 1);
      } else if (i + 1 < options.size()) {
        standard = options[i + 1];
      }
      standard = util::AsciiLower(standard);
      for (size_t pos = standard.find("gnu"); pos != std::string::npos;
           pos = standard.find("gnu", pos + 1)) {
        standard.replace(pos, 3, "c");
      }
      for (const auto& [from, to] : kStandards) {
        if (standard == from) {
          standard = to;
          break;
        }
      }
      params.push_back("--std=" + standard);
    }
  }
  return params;
}

}  // namespace

std::optional<util::SemVer> Cppcheck::Version() const {
  return BinaryVersion(
      "--version",
      [](const std::string& output) -> std::optional<util::SemVer> {
        static const util::Regex& kVersion =
            *new util::Regex(R"(^Cppcheck(.*?)(?P<version>[\d\.]+))");
        const std::optional<util::RegexMatch> match = kVersion.Match(output);
        if (!match) return std::nullopt;
        std::string version(*match->Group("version"));
        // "2.7" has no patch number.
        if (std::ranges::count(version, '.') < 2) version += ".0";
        return util::ParseSemVer(version);
      },
      kLog);
}

std::optional<std::string> Cppcheck::IncompatibleVersion() const {
  const std::optional<util::SemVer> version = Version();
  if (version && *version >= util::SemVer{.major = 1, .minor = 80}) {
    return std::nullopt;
  }
  return "CppCheck binary found is too old at v" +
         (version ? version->ToString() : std::string("None")) +
         "; minimum version is 1.80";
}

std::optional<std::string> Cppcheck::ResolveMissingBinary(
    const std::string& configured, const Environment& env) const {
  kLog.Debug(configured + " not found in path for Cppcheck!");
  if (configured.starts_with('/')) return std::nullopt;
  std::optional<std::string> cppcheck = GetBinaryInPath(
      {"cppcheck"}, R"(^cppcheck(-\d+(\.\d+){0,2})?$)", env);
  if (cppcheck) kLog.Debug("Using '" + *cppcheck + "' for Cppcheck!");
  return cppcheck;
}

std::vector<CheckerInfo> Cppcheck::Checkers() const {
  const std::optional<std::string> binary = Binary();
  if (!binary) return {};
  return checkers_.Get(*binary, [&binary]() -> std::vector<CheckerInfo> {
    const Result<std::string> output =
        CheckOutput({*binary, "--errorlist"}, platform::Output::kInherit);
    if (!output.ok()) {
      kLog.Error(output.error().message);
      return {};
    }
    std::vector<CheckerInfo> checkers;
    const Result<util::XmlDocument> document = util::ParseXml(output.value());
    if (!document.ok()) return {};
    xmlNode* errors = util::XmlFindChild(util::XmlRoot(document.value()),
                                         "errors");
    for (xmlNode* error : util::XmlChildren(errors)) {
      if (util::XmlTag(error) != "error") continue;
      const std::optional<std::string> id = util::XmlAttribute(error, "id");
      checkers.push_back(
          {.name = id && !id->empty() ? "cppcheck-" + *id : "",
           .description = util::XmlAttribute(error, "msg").value_or("")});
    }
    // Cppcheck reports with names that --errorlist does not list; the
    // checkers of the label file make them suppressible.
    const Result<AnalyzerContext*> context = GetContext();
    if (context.ok()) {
      std::set<std::string> known;
      for (const CheckerInfo& checker : checkers) known.insert(checker.name);
      for (const std::string& checker :
           context.value()->checker_labels().Checkers("cppcheck")) {
        if (known.insert(checker).second) {
          checkers.push_back({.name = checker, .description = ""});
        }
      }
    }
    return checkers;
  });
}

std::vector<AnalyzerOption> Cppcheck::AnalyzerConfigOptions() const {
  return {{.option = "addons",
           .documentation = "A list of cppcheck addon files."},
          {.option = "libraries",
           .documentation = "A list of cppcheck library definiton files."},
          {.option = "platform",
           .documentation = "The platform configuration .xml file."},
          {.option = "inconclusive",
           .documentation = "Enable inconclusive reports."},
          VerbatimArgsOption()};
}

Result<AnalyzerConfig> Cppcheck::ConstructConfig(const ConfigArgs& args) const {
  AnalyzerConfig config;
  config.analyzer = std::string(name());
  config.report_hash = args.report_hash;
  for (const AnalyzerConfigArg& item : args.analyzer_config) {
    if (item.analyzer == name()) {
      config.option_values[item.option].push_back(item.value);
    }
  }
  Result<std::vector<std::string>> extra =
      LoadVerbatimArgs(name(), args.analyzer_config, kLog);
  if (!extra.ok()) return Result<AnalyzerConfig>(extra.error());
  config.analyzer_extra_arguments = std::move(extra).value();
  if (Result<void> initialized = InitializeCheckers(Checkers(), args, &config);
      !initialized.ok()) {
    return Result<AnalyzerConfig>(initialized.error());
  }
  return Result<AnalyzerConfig>(std::move(config));
}

std::vector<std::string> Cppcheck::BuildCommand(AnalyzerRun* run) const {
  const AnalyzerConfig& config = *run->config;
  // All checks run; the disabled ones are suppressed.
  std::vector<std::string> command = {Binary().value_or(""), "--enable=all"};
  for (const CheckerEntry& checker : config.checkers.entries()) {
    if (checker.state != CheckerState::kDisabled) continue;
    std::string_view id = checker.name;
    if (id.starts_with("cppcheck-")) id.remove_prefix(9);
    command.push_back("--suppress=" + std::string(id));
  }
  // unusedFunction needs the whole program, not one source file.
  if (std::ranges::find(command, "--suppress=unusedFunction") ==
      command.end()) {
    command.push_back("--suppress=unusedFunction");
  }
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  std::vector<std::string> params = WhitelistedFlags(*run->action);
  const auto is_std = [](const std::string& arg) {
    return arg.starts_with("--std=");
  };
  if (std::ranges::any_of(config.analyzer_extra_arguments, is_std)) {
    if (const auto it = std::ranges::find_if(params, is_std);
        it != params.end()) {
      params.erase(it);
    }
  }
  command.insert(command.end(), params.begin(), params.end());
  const auto values = [&config](const std::string& option) {
    const auto it = config.option_values.find(option);
    return it == config.option_values.end() ? std::vector<std::string>()
                                            : it->second;
  };
  const std::vector<std::string> platform = values("platform");
  command.push_back("--platform=" +
                    (platform.empty() ? std::string("native") : platform[0]));
  if (config.option_values.contains("inconclusive")) {
    command.push_back("--inconclusive");
  }
  for (const std::string& addon : values("addons")) {
    command.push_back("--addon=" + addon);
  }
  for (const std::string& library : values("libraries")) {
    command.push_back("--library=" + library);
  }
  // Cppcheck does not create its output folder.
  const std::string output_dir =
      ActionOutputDir(*run, "cppcheck");
  std::error_code error;
  std::filesystem::create_directory(output_dir, error);
  if (error) {
    kLog.Error(util::OsError(error.value(), output_dir).message);
    return {};
  }
  command.push_back("--plist-output=" + output_dir);
  command.push_back(run->source_file);
  return command;
}

void Cppcheck::PostAnalyze(AnalyzerRun* run) const {
  // Cppcheck puts an id into the output file name; the plist files of the
  // output folder are copied to the result file. The originals are renamed
  // to .bak, so that report parsing, which walks the folders, skips them.
  const std::string output_dir =
      ActionOutputDir(*run, "cppcheck");
  std::error_code error;
  std::vector<std::filesystem::path> plists;
  for (std::filesystem::recursive_directory_iterator it(output_dir, error),
       end;
       !error && it != end; it.increment(error)) {
    if (it->path().extension() == ".plist") plists.push_back(it->path());
  }
  for (const std::filesystem::path& plist : plists) {
    std::error_code copy_error =
        platform::CopyFile(plist.string(), run->result_file);
    if (!copy_error) {
      std::filesystem::rename(plist, plist.string() + ".bak", copy_error);
    }
    if (copy_error) {
      kLog.Error(util::OsError(copy_error.value(), plist.string()).message);
    }
  }
}

Result<void> Cppcheck::PostprocessResult(
    AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
    const common::ReviewStatusHandler* review_status) const {
  kLog.DebugAnalyzer(run->out);
  const report::HashType hash_type = ReportHashType(run->config->report_hash);
  std::set<std::string> checkers;
  for (const CheckerInfo& checker : Checkers()) checkers.insert(checker.name);
  std::vector<report::Report> reports;
  report::SourceFileCache sources;
  for (report::Report& report :
       report::ReadPlistFile(run->result_file, SourceDirPath(*run))) {
    if (!report.checker_name.starts_with("cppcheck-")) {
      report.checker_name = "cppcheck-" + report.checker_name;
    }
    // Informational messages of Cppcheck are not checker reports.
    if ((skip_handlers != nullptr &&
         common::ShouldSkip(*skip_handlers, report.file.original_path)) ||
        !checkers.contains(report.checker_name)) {
      continue;
    }
    report.report_hash = report::GetReportHash(report, hash_type, sources);
    const report::BugPathEvent last = {
        .message = report.message,
        .file = report.file,
        .line = report.line,
        .column = report.column,
        .range = report::Range{.start_line = report.line,
                               .start_col = report.column,
                               .end_line = report.line,
                               .end_col = report.column}};
    if (report.bug_path_events.empty() ||
        !(last == report.bug_path_events.back())) {
      report.bug_path_events.push_back(last);
    }
    reports.push_back(std::move(report));
  }
  return WriteResultFiles(*run, std::move(reports), name(), review_status);
}

const Cppcheck& CppcheckAnalyzer() {
  static const Cppcheck& analyzer = *new Cppcheck;
  return analyzer;
}

}  // namespace cc::analyzer
