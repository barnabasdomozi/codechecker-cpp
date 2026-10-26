#include "analyzer/analysis/analysis_manager.h"

#include <stddef.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "analyzer/context/run_command.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/stats/statistics.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "tu_collector/tu_collector.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/parallel.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/zip.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

// The result of one job (Python: the tuple check() returns).
struct JobResult {
  int return_code = 1;
  bool reanalyzed = false;  // The result file existed before the job.
  std::string analyzer;
  std::string source;
};

// The folders of the job outputs (Python: output_dirs).
struct OutputDirs {
  std::string success;
  std::string failed;
  std::string reproducer;
  std::string ctu_connections;
};

// Build actions by (source, target), for the files that the analyzer
// output mentions (Python: create_actions_map()).
using ActionsMap =
    std::map<std::pair<std::string, std::string>, const BuildAction*>;

// Returns `text` with every "\ " replaced by " ".
std::string Unescape(const std::string& text) {
  std::string result;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == ' ') continue;
    result += text[i];
  }
  return result;
}

// Returns `command` as Python's repr() of the list.
std::string ListRepr(const std::vector<std::string>& command) {
  std::vector<std::string> items;
  for (const std::string& arg : command) items.push_back(util::Repr(arg));
  return "[" + util::Join(items, ", ") + "]";
}

// Python: SourceAnalyzer.analyze(): runs the command of `run` and stores
// its output. `killed` tells whether the timeout ended it.
void Analyze(const AnalyzerDriver& driver,
             const std::vector<std::string>& command,
             std::optional<int> timeout, AnalyzerRun* run, bool* killed) {
  kLog.Debug("Running analyzer ...");
  kLog.DebugAnalyzer("\n" + util::ShlexJoin(command));
  run->analyzer_cmd = command;
  if (command.empty()) {
    kLog.Error("list index out of range");
    run->return_code = 1;
    return;
  }
  const Environment env = driver.CommandEnvironment(command);
  kLog.Debug("\nexecuting:" + ListRepr(command) + "\n");
  RunProcOptions options = {.cwd = run->action->directory, .env = env};
  if (timeout && *timeout > 0) {
    options.timeout = std::chrono::seconds(*timeout);
    options.on_timeout = [killed](int pid) {
      kLog.Debug(std::format("Process {} has ran for too long, killing it!",
                             pid));
      *killed = true;
      platform::KillProcessTree(pid);
    };
  }
  const Result<CommandOutput> output = RunProc(command, options);
  if (!output.ok()) {
    kLog.Error(output.error().message);
    run->return_code = 1;
    return;
  }
  run->return_code = output.value().return_code;
  run->out = output.value().out;
  run->err = output.value().err;
}

// Python: save_output().
void SaveOutput(const std::string& base, const AnalyzerRun& run) {
  if (!run.out.empty()) (void)util::WriteFile(base + ".stdout.txt", run.out);
  if (!run.err.empty()) (void)util::WriteFile(base + ".stderr.txt", run.err);
}

// Python: handle_success(): the result file and its .source side file.
Result<void> HandleSuccess(const AnalyzerDriver& driver, AnalyzerRun* run,
                           const std::string& result_file,
                           const std::string& result_base,
                           const WorkerOptions& options,
                           const OutputDirs& dirs) {
  if (options.capture_analysis_output) {
    SaveOutput(util::pypath::Join({dirs.success, result_base}), *run);
  }
  if (Result<void> processed = driver.PostprocessResult(
          run, options.filter_handlers, options.review_status);
      !processed.ok()) {
    return processed;
  }
  // Python: save_metadata().
  if (Result<void> written = util::WriteFile(
          result_file + ".source", Unescape(run->source_file) + "\n");
      !written.ok()) {
    return written;
  }
  if (util::pypath::Exists(run->result_file) &&
      !util::pypath::Exists(result_file)) {
    std::rename(run->result_file.c_str(), result_file.c_str());
  }
  return Result<void>();
}

// Python: handle_failure(): compiler errors still become reports when the
// clang-diagnostic-error checker is on.
Result<void> HandleFailure(const AnalyzerDriver& driver, AnalyzerRun* run,
                           const std::string& result_base,
                           const WorkerOptions& options) {
  const CheckerEntry* error_checker =
      run->config->checkers.Find("clang-diagnostic-error");
  if (error_checker == nullptr ||
      error_checker->state == CheckerState::kEnabled) {
    if (Result<void> processed = driver.PostprocessResult(
            run, options.skip_handlers, options.review_status);
        !processed.ok()) {
      return processed;
    }
  }
  // Python means to delete the stale result file, but it looks for
  // "<name>.plist.plist" in the current folder.
  const std::string stale = result_base + ".plist";
  if (util::pypath::Exists(stale)) std::remove(stale.c_str());
  return Result<void>();
}

// Python: handle_reproducer(): the debug ZIP of a run.
Result<void> HandleReproducer(const AnalyzerDriver& driver, AnalyzerRun* run,
                              const std::string& zip_file,
                              const ActionsMap& actions_map) {
  const BuildAction& action = *run->action;
  std::set<std::string> other_files = driver.MentionedFiles(run, run->out);
  other_files.merge(driver.MentionedFiles(run, run->err));
  kLog.Debug("Collecting debug data");
  std::vector<tu_collector::CompileAction> build_actions = {
      {.file = action.source,
       .command = action.original_command,
       .directory = action.directory}};
  for (const std::string& file : other_files) {
    const std::string mentioned =
        util::pypath::Abspath(util::pypath::Join({action.directory, file}));
    const auto it = actions_map.find({mentioned, action.target});
    if (it == actions_map.end()) {
      kLog.Debug(std::format("Could not find ('{}', '{}') in build actions.",
                             mentioned, action.target));
      continue;
    }
    build_actions.push_back({.file = it->second->source,
                             .command = it->second->original_command,
                             .directory = it->second->directory});
  }
  if (Result<void> zipped = tu_collector::ZipTuFiles(zip_file, build_actions);
      !zipped.ok()) {
    return zipped;
  }
  std::set<std::string> paths;
  for (const std::string& file : other_files) {
    paths.insert(util::pypath::Join({action.directory, file}));
  }
  if (Result<void> added = tu_collector::AddSourcesToZip(zip_file, paths);
      !added.ok()) {
    return added;
  }
  Result<util::ZipArchive> archive =
      util::ZipArchive::Open(zip_file, util::ZipArchive::Mode::kAppend);
  if (!archive.ok()) return Result<void>(archive.error());
  util::ZipArchive& zip = archive.value();
  std::vector<std::string> quoted;
  for (const std::string& arg : run->analyzer_cmd) {
    quoted.push_back(util::ShlexQuote(arg));
  }
  for (const auto& [name, text] :
       {std::pair<const char*, std::string>("stdout", run->out),
        {"stderr", run->err},
        {"build-action", action.original_command},
        {"analyzer-command", util::Join(quoted, " ")},
        {"return-code", std::to_string(run->return_code)}}) {
    if (Result<void> added = zip.AddText(name, text); !added.ok()) return added;
  }
  // Python: gcc_toolchain.toolchain_in_args().
  static const util::Regex& kToolchain =
      *new util::Regex("^--gcc-toolchain=(?P<tcpath>.*)$");
  const Result<std::vector<std::string>> args =
      util::ShlexSplit(action.original_command);
  for (const std::string& arg :
       args.ok() ? args.value() : std::vector<std::string>()) {
    if (arg.find("--gcc-toolchain") == std::string::npos) continue;
    if (const auto match = kToolchain.Match(arg)) {
      if (Result<void> added = zip.AddText(
              "gcc-toolchain-path", std::string(*match->Group("tcpath")));
          !added.ok()) {
        return added;
      }
      break;
    }
  }
  const std::string compiler_info =
      util::pypath::Join({run->workspace, "compiler_info.json"});
  if (util::pypath::Exists(compiler_info)) {
    const Result<std::string> content = util::ReadFile(compiler_info);
    if (content.ok()) {
      if (Result<void> added = zip.AddText("compiler_info.json",
                                           content.value());
          !added.ok()) {
        return added;
      }
    }
  }
  kLog.Debug("ZIP file written at '" + zip_file + "'");
  return zip.Close();
}

// Python: collect_ctu_involved_files().
void CollectCtuInvolvedFiles(const AnalyzerDriver& driver, AnalyzerRun* run,
                             const std::string& output_dir) {
  if (driver.name() != "clangsa") return;
  std::set<std::string> files = driver.MentionedFiles(run, run->out);
  files.merge(driver.MentionedFiles(run, run->err));
  const std::string out = util::pypath::Join({output_dir, run->action_str});
  if (!files.empty()) {
    (void)util::WriteFile(
        out, util::Join(std::vector<std::string>(files.begin(), files.end()),
                        "\n"));
  } else if (util::pypath::Exists(out)) {
    std::remove(out.c_str());
  }
}

// The job number of the progress lines, taken when it is first needed.
class Progress {
 public:
  explicit Progress(std::atomic<int>* counter) : counter_(counter) {}

  Progress(const Progress&) = delete;
  Progress& operator=(const Progress&) = delete;

  int number() {
    if (number_ == 0) number_ = counter_->fetch_add(1);
    return number_;
  }

 private:
  std::atomic<int>* counter_;
  int number_ = 0;
};

// Python: check(), one job. Errors that Python raises in the job end it
// with return code 1.
Result<JobResult> Check(const BuildAction& action, const AnalyzerConfig& config,
                        const WorkerOptions& options, const OutputDirs& dirs,
                        const ActionsMap& actions_map, Progress* progress,
                        int total) {
  const AnalyzerDriver& driver = *FindAnalyzer(action.analyzer_type);
  Result<AnalyzerRun> prepared =
      PrepareRun(action, config, options.output_path, options.statistics,
                 /*disable_ctu=*/false);
  if (!prepared.ok()) return Result<JobResult>(prepared.error());
  AnalyzerRun run = std::move(prepared).value();
  const bool result_file_exists = util::pypath::Exists(run.result_file);
  bool killed = false;
  Analyze(driver, driver.BuildCommand(&run), options.timeout, &run, &killed);
  if (killed) {
    kLog.Warning(std::format("Analyzer ran too long, exceeding time limit of "
                             "{} seconds.",
                             *options.timeout));
    kLog.Warning("Considering this analysis as failed...");
    run.return_code = -1;
    run.err = std::format(">>> CodeChecker: Analysis timed out after {} "
                          "seconds. <<<\n{}",
                          *options.timeout, run.err);
  }
  driver.PostAnalyze(&run);

  // Clang SA writes the file name without "\ " escapes; Clang-Tidy keeps
  // them, so they are removed for every analyzer.
  const std::string result_file = Unescape(run.result_file);
  const std::string result_base = util::pypath::Basename(result_file);
  const bool ctu_active = driver.name() == "clangsa" &&
                          !config.ctu_dir.empty() && !run.disable_ctu;
  std::string failure = "_unknown";
  if (run.return_code == 1) failure = "_compile_error";
  if (run.return_code == 254) failure = "_crash";
  if (run.return_code == 0) failure = "";
  const std::string zip_file =
      result_base + (ctu_active ? "_CTU" : "") + failure + ".zip";
  for (const std::string& dir : {dirs.failed, dirs.reproducer}) {
    const std::string old_zip = util::pypath::Join({dir, zip_file});
    if (util::pypath::Exists(old_zip)) std::remove(old_zip.c_str());
  }
  const std::string source_name = util::pypath::Basename(action.source);

  // Python: handle_analysis_result().
  const auto handle_result = [&](AnalyzerRun* current, bool success,
                                 const std::string& zip) -> Result<void> {
    Result<void> handled =
        success ? HandleSuccess(driver, current, result_file, result_base,
                                options, dirs)
                : HandleFailure(driver, current, result_base, options);
    if (!handled.ok()) return handled;
    if (success && !options.generate_reproducer) return Result<void>();
    const std::string& zip_dir =
        options.generate_reproducer ? dirs.reproducer : dirs.failed;
    return HandleReproducer(driver, current, util::pypath::Join({zip_dir, zip}),
                            actions_map);
  };

  int return_code = run.return_code;
  bool reanalyzed = result_file_exists;
  AnalyzerRun* current = &run;
  AnalyzerRun retry;
  if (run.return_code == 0) {
    if (Result<void> handled =
            handle_result(&run, /*success=*/true, zip_file);
        !handled.ok()) {
      return Result<JobResult>(handled.error());
    }
    kLog.Info(std::format("[{}/{}] {} analyzed {} successfully.",
                          progress->number(), total, action.analyzer_type,
                          source_name));
    if (result_file_exists) {
      kLog.Debug("Previous analysis results in '" + run.result_file +
                 "' has been overwritten.");
    }
  } else {
    kLog.Error(std::format("Analyzing {} with {} {} failed!", source_name,
                           action.analyzer_type, ctu_active ? "CTU" : ""));
    if (!options.quiet) {
      kLog.Error("\n" + run.out);
      kLog.Error("\n" + run.err);
    }
    if (Result<void> handled =
            handle_result(&run, /*success=*/false, zip_file);
        !handled.ok()) {
      return Result<JobResult>(handled.error());
    }
    if (ctu_active && options.ctu_reanalyze_on_failure) {
      kLog.Error("Try to reanalyze without CTU");
      Result<AnalyzerRun> again =
          PrepareRun(action, config, options.output_path, options.statistics,
                     /*disable_ctu=*/true);
      if (!again.ok()) return Result<JobResult>(again.error());
      retry = std::move(again).value();
      current = &retry;
      reanalyzed = util::pypath::Exists(retry.result_file);
      bool retry_killed = false;
      Analyze(driver, driver.BuildCommand(&retry), std::nullopt, &retry,
              &retry_killed);
      return_code = retry.return_code;
      if (retry.return_code == 0) {
        if (Result<void> handled =
                handle_result(&retry, /*success=*/true, zip_file);
            !handled.ok()) {
          return Result<JobResult>(handled.error());
        }
        kLog.Info(std::format("[{}/{}] {} analyzed {} without CTU "
                              "successfully.",
                              progress->number(), total, action.analyzer_type,
                              source_name));
        if (result_file_exists) {
          kLog.Debug("Previous analysis results in '" + retry.result_file +
                     "' has been overwritten.");
        }
      } else {
        kLog.Error(std::format("Analyzing '{}' with {} without CTU failed.",
                               source_name, action.analyzer_type));
        if (Result<void> handled =
                handle_result(&retry, /*success=*/false, result_base + ".zip");
            !handled.ok()) {
          return Result<JobResult>(handled.error());
        }
      }
    }
  }
  CollectCtuInvolvedFiles(driver, current, dirs.ctu_connections);
  if (!options.quiet) {
    if (current->return_code != 0) {
      kLog.Error("\n" + current->out);
      kLog.Error("\n" + current->err);
    } else {
      kLog.DebugAnalyzer("\n" + current->out);
      kLog.DebugAnalyzer("\n" + current->err);
    }
  }
  return Result<JobResult>(JobResult{.return_code = return_code,
                                     .reanalyzed = reanalyzed,
                                     .analyzer = action.analyzer_type,
                                     .source = action.source});
}

// Python: print_analyzer_statistic_summary().
void PrintSummary(const MetadataTool& metadata, bool successful,
                  std::string_view heading) {
  const auto count = [successful](const AnalyzerMetadata& analyzer) {
    return successful ? analyzer.statistics.successful
                      : analyzer.statistics.failed;
  };
  bool any = false;
  for (const auto& [name, analyzer] : metadata.analyzers) {
    any = any || count(analyzer) != 0;
  }
  if (any) kLog.Info(heading);
  for (const auto& [name, analyzer] : metadata.analyzers) {
    if (count(analyzer) != 0) {
      kLog.Info(std::format("  {}: {}", name, count(analyzer)));
    }
  }
}

// Python: worker_result_handler(): the summary and the result files.
void HandleResults(const std::vector<JobResult>& results,
                   const std::string& output_path, MetadataTool* metadata) {
  int reanalyzed = 0;
  for (const JobResult& result : results) {
    if (result.reanalyzed) ++reanalyzed;
    for (auto& [name, analyzer] : metadata->analyzers) {
      if (name != result.analyzer) continue;
      AnalyzerStatistics& stats = analyzer.statistics;
      if (result.return_code == 0) {
        ++stats.successful;
        stats.successful_sources.push_back(result.source);
      } else {
        ++stats.failed;
        stats.failed_sources.push_back(result.source);
      }
    }
  }
  kLog.Info("----==== Summary ====----");
  PrintSummary(*metadata, /*successful=*/true, "Successfully analyzed");
  PrintSummary(*metadata, /*successful=*/false, "Failed to analyze");
  if (reanalyzed != 0) {
    kLog.Info(std::format("Reanalyzed compilation commands: {}", reanalyzed));
  }
  metadata->skipped = 0;
  // The .source side files of the jobs go into the metadata.
  std::vector<std::pair<std::string, std::string>> sources;
  for (const std::string& name : platform::ListDirectory(output_path)) {
    if (!name.ends_with(".source") || name.starts_with('.')) continue;
    const std::string path = util::pypath::Join({output_path, name});
    const Result<std::string> content = util::ReadFile(path);
    sources.emplace_back(path.substr(0, path.size() - 7),
                         content.ok() ? std::string(util::Strip(
                                            util::DecodeUtf8Ignore(
                                                content.value())))
                                      : "");
    std::remove(path.c_str());
  }
  const std::string failed = util::pypath::Join({output_path, "failed"});
  for (const std::string& name : platform::ListDirectory(failed)) {
    if (!name.ends_with(".error") || name.starts_with('.')) continue;
    RemoveResultSource(
        util::pypath::Join(
            {output_path, util::pypath::Splitext(name).first + ".plist"}),
        metadata);
  }
  for (const auto& [result_file, source] : sources) {
    SetResultSource(result_file, source, metadata);
  }
}

}  // namespace

Result<AnalyzerRun> PrepareRun(const BuildAction& action,
                               const AnalyzerConfig& config,
                               const std::string& output_path,
                               const std::optional<StatisticsData>& statistics,
                               bool disable_ctu) {
  kLog.Debug("Constructing " + action.analyzer_type + " analyzer.");
  Result<AnalyzerRun> run = MakeAnalyzerRun(action, config, output_path);
  if (!run.ok()) return run;
  AnalyzerRun value = std::move(run).value();
  value.disable_ctu = disable_ctu;
  if (action.analyzer_type == "clangsa" && statistics) {
    // Both statistics checkers use the same folder.
    if (util::pypath::Exists(
            SpecialReturnStatsFile(statistics->stats_out_dir))) {
      value.checker_configs.push_back(
          StatisticsAnalyzeConfig(statistics->stats_out_dir));
    } else {
      kLog.Debug("No checker statistics file was found for "
                 "statisticsbased.SpecialReturnValue");
    }
  }
  return Result<AnalyzerRun>(std::move(value));
}

void StartWorkers(const std::vector<BuildAction>& all_actions,
                  const std::map<std::string, AnalyzerConfig>& configs,
                  const WorkerOptions& options,
                  const CompileCommandCount& count, MetadataTool* metadata) {
  std::vector<const BuildAction*> actions;
  std::vector<const BuildAction*> skipped;
  for (const BuildAction& action : all_actions) {
    if (options.skip_handlers != nullptr &&
        common::ShouldSkip(*options.skip_handlers, action.source)) {
      skipped.push_back(&action);
    } else {
      actions.push_back(&action);
    }
  }
  const OutputDirs dirs = {
      .success = util::pypath::Join({options.output_path, "success"}),
      .failed = util::pypath::Join({options.output_path, "failed"}),
      .reproducer = util::pypath::Join({options.output_path, "reproducer"}),
      .ctu_connections =
          util::pypath::Join({options.output_path, "ctu_connections"})};
  std::vector<std::string> output_dirs = {
      dirs.failed, dirs.success,
      util::pypath::Join({options.output_path, "cppcheck"}),
      dirs.ctu_connections};
  if (options.generate_reproducer) output_dirs.push_back(dirs.reproducer);
  for (const std::string& dir : output_dirs) {
    if (const std::error_code error = platform::EnsureDirs(dir)) {
      kLog.Error(util::OsError(error.value(), dir).message);
    }
  }
  ActionsMap actions_map;
  for (const BuildAction& action : all_actions) {
    actions_map[{action.source, action.target}] = &action;
  }

  if (!actions.empty()) {
    std::vector<JobResult> results(actions.size());
    std::atomic<int> counter = 1;
    const int total = static_cast<int>(actions.size());
    util::ParallelFor(options.jobs, actions.size(), [&](size_t i) {
      const BuildAction& action = *actions[i];
      Progress progress(&counter);
      Result<JobResult> result =
          Check(action, configs.at(action.analyzer_type), options, dirs,
                actions_map, &progress, total);
      if (!result.ok()) {
        kLog.Debug(result.error().message);
        if (!result.error().message.empty()) {
          util::Print(result.error().message);
        }
        results[i] = {.return_code = 1,
                      .analyzer = action.analyzer_type,
                      .source = action.source};
      } else {
        results[i] = std::move(result).value();
      }
      progress.number();
    });
    HandleResults(results, options.output_path, metadata);
  } else {
    kLog.Info("----==== Summary ====----");
  }

  for (const BuildAction* action : skipped) {
    kLog.Debug(action->source + " is skipped");
  }
  kLog.Info(std::format("Total analyzed compilation commands: {}",
                        count.analyze));
  if (!skipped.empty() || count.skipped != 0) {
    kLog.Info(std::format("Skipped compilation commands: {}",
                          count.skipped + static_cast<int>(skipped.size())));
  }
  kLog.Info("----=================----");
  for (const std::string& dir : {dirs.success, dirs.failed}) {
    if (platform::ListDirectory(dir).empty()) platform::RemoveTree(dir);
  }
}

}  // namespace cc::analyzer
