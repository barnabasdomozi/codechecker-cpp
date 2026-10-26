#include "analyzer/drivers/infer.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "analyzer/buildlog/log_parser.h"
#include "analyzer/context/analyzer_context.h"
#include "analyzer/context/run_command.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/result_handler.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "report/converter/json_formats.h"
#include "report/model/report.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/semver.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer.infer");

// Returns the environment of the Infer binary `binary`, with TZ=UTC.
std::vector<std::string> InferEnvironment(const std::string& binary) {
  const Result<AnalyzerContext*> context = GetContext();
  Environment env =
      context.ok() ? context.value()->EnvForBin(binary) : std::nullopt;
  std::vector<std::string> result =
      env ? std::move(*env) : platform::CurrentEnvironment();
  platform::SetEnv(&result, "TZ", "UTC");
  return result;
}

// Returns the output folder of Infer for `run`.
std::string OutputDir(const AnalyzerRun& run) {
  return ActionOutputDir(run, "infer");
}

}  // namespace

std::optional<util::SemVer> Infer::Version() const {
  const std::optional<std::string> binary = Binary();
  if (!binary) return std::nullopt;
  return versions_.Get(*binary, [&binary]() -> std::optional<util::SemVer> {
    const platform::RunResult result = platform::RunProcess(
        {*binary, "--version"},
        {.env = InferEnvironment(*binary),
         .stderr_mode = platform::Output::kInherit});
    if (result.start_error != 0 || result.return_code != 0) {
      kLog.Warning("Failed to get analyzer version: " + *binary +
                   " --version");
      return std::nullopt;
    }
    const std::string output = util::DecodeUtf8Ignore(result.out);
    // The last word of the first line, without its "v".
    const std::vector<std::string> words =
        util::Split(util::Strip(output.substr(0, output.find('\n'))), " ");
    return util::ParseSemVer(std::string_view(words.back()).substr(
        words.back().empty() ? 0 : 1));
  });
}

std::vector<CheckerInfo> Infer::Checkers() const {
  const std::optional<std::string> binary = Binary();
  if (!binary) return {};
  return checkers_.Get(*binary, [this, &binary]() -> std::vector<CheckerInfo> {
    const platform::RunResult result = platform::RunProcess(
        {*binary, "help", "--list-issue-types"},
        {.env = InferEnvironment(*binary),
         .stderr_mode = platform::Output::kDevNull});
    if (result.start_error != 0 || result.return_code != 0) {
      kLog.Error(std::to_string(result.start_error));
      return {};
    }
    const Result<AnalyzerContext*> context = GetContext();
    std::vector<CheckerInfo> checkers;
    for (const std::string& entry :
         util::Split(util::DecodeUtf8Ignore(result.out), "\n")) {
      const std::vector<std::string> data =
          util::Split(util::Strip(entry), ":");
      if (data.size() < 7) continue;
      std::string checker = "infer-" + util::AsciiLower(data[0]);
      std::ranges::replace(checker, '_', '-');
      std::string description =
          context.ok() ? context.value()->checker_labels().UniqueLabelOfChecker(
                             checker, "description", name())
                       : "";
      if (description.empty()) {
        description = "used by '" + (data.size() == 7 ? data[6] : data[5]) +
                      "' checker";
      }
      checkers.push_back({.name = std::move(checker),
                          .description = std::move(description)});
    }
    return checkers;
  });
}

std::vector<AnalyzerOption> Infer::AnalyzerConfigOptions() const {
  return {VerbatimArgsOption()};
}

Result<AnalyzerConfig> Infer::ConstructConfig(const ConfigArgs& args) const {
  AnalyzerConfig config;
  config.analyzer = std::string(name());
  config.report_hash = args.report_hash;
  for (const AnalyzerConfigArg& item : args.analyzer_config) {
    if (item.analyzer == name()) {
      config.option_values[item.option].push_back(item.value);
    }
  }
  if (Result<void> initialized = InitializeCheckers(Checkers(), args, &config);
      !initialized.ok()) {
    return Result<AnalyzerConfig>(initialized.error());
  }
  Result<std::vector<std::string>> extra =
      LoadVerbatimArgs(name(), args.analyzer_config, kLog);
  if (!extra.ok()) return Result<AnalyzerConfig>(extra.error());
  config.analyzer_extra_arguments = std::move(extra).value();
  return Result<AnalyzerConfig>(std::move(config));
}

std::vector<std::string> Infer::BuildCommand(AnalyzerRun* run) const {
  const AnalyzerConfig& config = *run->config;
  std::vector<std::string> command = {Binary().value_or(""), "run",
                                      "--keep-going", "--project-root", "/"};
  for (const CheckerEntry& checker : config.checkers.entries()) {
    std::string issue = checker.name;
    if (issue.starts_with("infer-")) issue.erase(0, 6);
    std::ranges::replace(issue, '-', '_');
    for (char& c : issue) {
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    command.push_back(checker.state == CheckerState::kDisabled
                          ? "--disable-issue-type"
                          : "--enable-issue-type");
    command.push_back(issue);
  }
  const std::string output_dir = OutputDir(*run);
  if (const std::error_code error = platform::EnsureDirs(output_dir)) {
    kLog.Error(util::OsError(error.value(), output_dir).message);
    return {};
  }
  command.insert(command.end(), {"-o", output_dir});
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  command.push_back("--");
  const bool c_family = run->action->lang == "c" || run->action->lang == "c++";
  Result<std::vector<std::string>> args =
      util::ShlexSplit(run->action->original_command);
  if (args.ok()) {
    for (std::string& arg : std::move(args).value()) {
      if (c_family && IgnoredOptionsGcc().Match(arg)) continue;
      command.push_back(std::move(arg));
    }
  }
  if (run->action->lang == "c++") command.push_back("-stdlib=libc++");
  kLog.DebugAnalyzer("Running analysis command '" + util::ShlexJoin(command) +
                     "'");
  return command;
}

Environment Infer::CommandEnvironment(
    const std::vector<std::string>& command) const {
  return InferEnvironment(command[0]);
}

void Infer::PostAnalyze(AnalyzerRun* run) const {
  if (run->return_code != 0) kLog.Error(run->err);
}

Result<void> Infer::PostprocessResult(
    AnalyzerRun* run, const common::SkipListHandlers* /*skip_handlers*/,
    const common::ReviewStatusHandler* review_status) const {
  kLog.DebugAnalyzer(run->out);
  const std::string output_dir = OutputDir(*run);
  Result<std::vector<report::Report>> parsed = report::GetInferReports(
      util::pypath::Join({output_dir, "report.json"}));
  std::vector<report::Report> reports =
      parsed.ok() ? std::move(parsed).value() : std::vector<report::Report>();
  for (report::Report& report : reports) {
    if (!report.checker_name.starts_with("infer-")) {
      std::string nicer = util::AsciiLower(report.checker_name);
      std::ranges::replace(nicer, '_', '-');
      report.checker_name = "infer-" + nicer;
    }
  }
  SetReportHashes(*run, &reports);
  if (Result<void> written =
          WriteResultFiles(*run, std::move(reports), name(), review_status);
      !written.ok()) {
    return written;
  }
  platform::RemoveTree(output_dir);
  return Result<void>();
}

const Infer& InferAnalyzer() {
  static const Infer& analyzer = *new Infer;
  return analyzer;
}

}  // namespace cc::analyzer
