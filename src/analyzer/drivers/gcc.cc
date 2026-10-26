#include "analyzer/drivers/gcc.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/context/run_command.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/flag.h"
#include "analyzer/drivers/result_handler.h"
#include "platform/filesystem.h"
#include "report/model/report.h"
#include "report/parser/sarif.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/semver.h"
#include "util/shlex.h"
#include "util/strings.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer.gcc");

// Returns `name` with the first `from` replaced by `to`.
std::string ReplaceFirst(std::string name, std::string_view from,
                         std::string_view to) {
  if (const size_t pos = name.find(from); pos != std::string::npos) {
    name.replace(pos, from.size(), to);
  }
  return name;
}

}  // namespace

std::optional<util::SemVer> Gcc::Version() const {
  return BinaryVersion(
      "-dumpfullversion",
      [](const std::string& output) {
        return util::ParseSemVer(util::Strip(output));
      },
      kLog);
}

std::optional<std::string> Gcc::IncompatibleVersion() const {
  const std::optional<util::SemVer> version = Version();
  if (!version) return "GCC binary is too old to support -dumpfullversion.";
  // -fdiagnostics-format=sarif-stderr came in GCC 13.
  if (*version >= util::SemVer{.major = 13}) return std::nullopt;
  return "GCC binary found is too old at v" + version->ToString() +
         "; minimum version is 13.0.0.";
}

std::vector<CheckerInfo> Gcc::Checkers() const {
  const std::optional<std::string> binary = Binary();
  if (!binary) return {};
  return checkers_.Get(*binary, [this, &binary]() -> std::vector<CheckerInfo> {
    const Result<std::string> output =
        CheckOutput({*binary, "--help=warning"}, platform::Output::kInherit);
    if (!output.ok()) {
      kLog.Error(output.error().message);
      return {};
    }
    const Result<AnalyzerContext*> context = GetContext();
    const std::vector<std::string> blacklisted =
        context.ok() ? context.value()->checker_labels().CheckersByLabels(
                           {"blacklist:true"}, name())
                     : std::vector<std::string>();
    std::vector<CheckerInfo> checkers;
    for (const std::string& entry : util::Split(output.value(), "\n")) {
      const std::string_view line = util::Strip(entry);
      const size_t space = line.find(' ');
      const std::string warning(line.substr(0, space));
      const std::string_view description =
          space == std::string_view::npos ? "" : line.substr(space + 1);
      if (!warning.starts_with("-W") ||
          warning.find('=') != std::string::npos || warning == "-W") {
        continue;
      }
      const std::string checker = ReplaceFirst(warning, "-W", "gcc-");
      if (std::ranges::find(blacklisted, checker) != blacklisted.end()) {
        continue;
      }
      checkers.push_back(
          {.name = checker,
           .description = std::string(util::Strip(description))});
    }
    return checkers;
  });
}

std::vector<AnalyzerOption> Gcc::AnalyzerConfigOptions() const {
  return {VerbatimArgsOption()};
}

Result<AnalyzerConfig> Gcc::ConstructConfig(const ConfigArgs& args) const {
  AnalyzerConfig config;
  config.analyzer = std::string(name());
  config.report_hash = args.report_hash;
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

std::vector<std::string> Gcc::BuildCommand(AnalyzerRun* run) const {
  const std::optional<std::string> binary = Binary();
  if (!binary) return {};
  const AnalyzerConfig& config = *run->config;
  // -fsyntax-only would stop too early for the analyzer, so the object file
  // goes to /dev/null.
  std::vector<std::string> command = {*binary, "-fanalyzer", "-c",
                                      "-o/dev/null"};
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  command.insert(command.end(), run->action->analyzer_options.begin(),
                 run->action->analyzer_options.end());
  command.push_back("-fdiagnostics-format=sarif-stderr");
  for (const CheckerEntry& checker : config.checkers.entries()) {
    command.push_back(ReplaceFirst(
        checker.name, "gcc-",
        checker.state == CheckerState::kDisabled ? "-Wno-" : "-W"));
  }
  if (!HasFlag("-x", command)) {
    command.insert(command.end(), {"-x", run->action->lang});
  }
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  command.push_back(run->source_file);
  kLog.DebugAnalyzer("Running analysis command '" + util::ShlexJoin(command) +
                     "'");
  return command;
}

Result<void> Gcc::PostprocessResult(
    AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
    const common::ReviewStatusHandler* review_status) const {
  kLog.DebugAnalyzer(run->out);
  if (run->err.empty()) {
    return Result<void>(Error{
        .message = "AssertionError: Even in the event of no reported "
                   "diagnostics, stderr mustn't be empty!"});
  }
  // The SARIF output is parsed from a file, renamed to .bak afterwards so
  // that report parsing skips it.
  const std::string folder = util::pypath::Join({run->workspace, "gcc"});
  std::error_code error = platform::EnsureDirs(folder);
  if (error) return Result<void>(util::OsError(error.value(), folder));
  const std::string sarif = util::pypath::Join(
      {folder, util::pypath::Basename(run->source_file) + run->action_hash +
                   ".sarif"});
  if (Result<void> written = util::WriteFile(sarif, run->err); !written.ok()) {
    return written;
  }
  std::vector<report::Report> reports = report::ReadSarifFile(sarif);
  DropSkippedReports(skip_handlers, &reports);
  std::erase_if(reports, [](const report::Report& report) {
    return !report.checker_name.starts_with("-Wanalyzer");
  });
  std::filesystem::rename(sarif, sarif + ".bak", error);
  if (error) kLog.Error(util::OsError(error.value(), sarif).message);
  for (report::Report& report : reports) {
    report.checker_name = ReplaceFirst(report.checker_name, "-W", "gcc-");
  }
  SetReportHashes(*run, &reports);
  return WriteResultFiles(*run, std::move(reports), name(), review_status);
}

const Gcc& GccAnalyzer() {
  static const Gcc& analyzer = *new Gcc;
  return analyzer;
}

}  // namespace cc::analyzer
