#include "analyzer/cli/parse.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/metadata/metadata.h"
#include "analyzer/suppress_handler.h"
#include "common/checker_labels.h"
#include "common/cli/verbose.h"
#include "common/cmd_config.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "common/source_code_comment_handler.h"
#include "common/util.h"
#include "platform/filesystem.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/output/baseline.h"
#include "report/output/checker_info.h"
#include "report/output/codeclimate.h"
#include "report/output/gerrit.h"
#include "report/output/html.h"
#include "report/output/json.h"
#include "report/output/plaintext.h"
#include "report/output/sarif.h"
#include "report/output/statistics.h"
#include "report/parser/report_file.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/fnmatch.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::analyzer::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");
// The logger of the report-converter functions that parse uses.
constexpr util::Logger kConverterLog("report-converter");

constexpr std::string_view kDescription = R"(
Parse and pretty-print the summary and results from one or more
'codechecker-analyze' result files. Bugs which are commented by using
"false_positive", "suppress" and "intentional" source code comments will not be
printed by the `parse` command.)";

// The end of the help, after the environment variables.
constexpr std::string_view kEpilogExitStatus = R"(

Exit status
------------------------------------------------

0 - No report
1 - CodeChecker error
2 - At least one report emitted by an analyzer

)";

constexpr std::string_view kExportTypes[] = {
    "html", "json", "codeclimate", "gerrit", "baseline", "sarif"};

// The options of `parse`.
struct ParseArgs {
  std::vector<std::string> inputs;
  std::optional<std::string> export_format;
  std::optional<std::string> output_path;
  std::optional<std::string> suppress;
  bool export_source_suppress = false;
  bool print_steps = false;
  std::vector<std::string> trim_path_prefixes;  // Empty: no trimming.
  std::optional<std::vector<std::string>> review_status;
  bool status = false;
  bool detailed = false;
  std::optional<std::string> skipfile;
  std::optional<std::vector<std::string>> files;
};

// Returns the options in `ns`.
ParseArgs ToParseArgs(const argparse::Namespace& ns) {
  const auto optional = [&ns](std::string_view dest) {
    return ns.Has(dest) ? std::optional<std::string>(ns.Get(dest))
                        : std::nullopt;
  };
  const auto list = [&ns](std::string_view dest) {
    return ns.Has(dest)
               ? std::optional<std::vector<std::string>>(ns.GetList(dest))
               : std::nullopt;
  };
  return ParseArgs{
      .inputs = ns.GetList("input"),
      .export_format = optional("export"),
      .output_path = optional("output_path"),
      .suppress = optional("suppress"),
      .export_source_suppress = ns.Has("create_suppress"),
      .print_steps = ns.Has("print_steps"),
      .trim_path_prefixes =
          list("trim_path_prefix").value_or(std::vector<std::string>()),
      .review_status = list("review_status"),
      .status = ns.Has("status"),
      .detailed = ns.Has("detailed"),
      .skipfile = optional("skipfile"),
      .files = list("files")};
}

// Returns `items` as Python's str() of a list of strings shows them.
std::string ListRepr(const std::vector<std::string>& items) {
  std::vector<std::string> reprs;
  for (const std::string& item : items) reprs.push_back(util::Repr(item));
  return "[" + util::Join(reprs, ", ") + "]";
}

// Returns the error for a Python exception that is not caught, so the
// command ends with a stack trace: the text of `error` is printed as it is,
// not logged.
Error Uncaught(const Error& error) {
  util::Print(error.message);
  return Error{};
}

// --- The analysis status (--status) ---

// The states of the analysis of a source file, in the order of the JSON
// output.
enum class AnalysisState { kUpToDate, kOutdated, kMissing, kFailed };

// The number of AnalysisState values.
constexpr int kStateCount = 4;

// Returns `state` as an index into arrays of kStateCount elements.
size_t Index(AnalysisState state) { return static_cast<size_t>(state); }

// The JSON keys of the states, by AnalysisState.
constexpr std::string_view kStateKeys[kStateCount] = {
    "up-to-date", "outdated", "missing", "failed"};

// A state and the title of its log lines.
struct StateTitle {
  AnalysisState state;
  std::string_view title;
};

// The states in the order of the log lines.
constexpr StateTitle kStateTitles[] = {
    {.state = AnalysisState::kUpToDate,
     .title = "Up-to-date analysis results"},
    {.state = AnalysisState::kOutdated, .title = "Outdated analysis results"},
    {.state = AnalysisState::kFailed, .title = "Failed to analyze"},
    {.state = AnalysisState::kMissing, .title = "Missing analysis results"}};

// The source files of one analyzer in each state.
struct AnalyzerStatus {
  std::string name;
  // By AnalysisState, each in the order first seen.
  std::array<std::vector<std::string>, kStateCount> files;
};

// The analysis status of a report folder (Python: get_report_dir_status()).
struct ReportDirStatus {
  // In the order of SupportedAnalyzers().
  std::vector<AnalyzerStatus> analyzers;
  int analyzed_commands = 0;
  int available_commands = 0;
};

// An entry of compile_cmd.json, its file made absolute.
struct CompileCommand {
  std::string file;
  std::string directory;
  std::string command;
};

// Returns the entries of <report_dir>/compile_cmd.json. Fails if the file
// cannot be read (which is logged as warnings) or has no entries.
Result<std::vector<CompileCommand>> LoadCompileCommands(
    const std::string& report_dir) {
  using Commands = Result<std::vector<CompileCommand>>;
  const std::string path =
      util::pypath::Join({report_dir, "compile_cmd.json"});
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  yyjson_val* root =
      document ? yyjson_doc_get_root(document->get()) : nullptr;
  if (!util::JsonTruthy(root)) {
    return Commands(Error{.message = std::format(
                              "Failed to load compile commands JSON '{}'.",
                              path)});
  }
  constexpr std::string_view kKeys[] = {"file", "directory", "command"};
  std::vector<CompileCommand> commands;
  yyjson_arr_iter iter = yyjson_arr_iter_with(root);
  while (yyjson_val* entry = yyjson_arr_iter_next(&iter)) {
    std::array<std::string, std::size(kKeys)> values;
    for (size_t i = 0; i < values.size(); ++i) {
      // Python ends with an uncaught KeyError here.
      const Result<yyjson_val*> value = util::JsonSubscript(entry, kKeys[i]);
      if (!value.ok()) return Commands(value.error());
      values[i] = util::JsonToStr(value.value());
    }
    // os.path.isabs().
    if (!values[0].starts_with('/')) {
      values[0] =
          util::pypath::Abspath(util::pypath::Join({values[1], values[0]}));
    }
    commands.push_back({.file = std::move(values[0]),
                        .directory = std::move(values[1]),
                        .command = std::move(values[2])});
  }
  return Commands(std::move(commands));
}

// Returns the state of the analysis of `source` whose result file is
// `plist`, from the names of the files in the report folder `report_dir`.
AnalysisState StateOf(const std::string& source, const std::string& plist,
                      const std::string& report_dir,
                      const std::set<std::string>& files) {
  if (files.contains(plist + ".err")) return AnalysisState::kFailed;
  if (!files.contains(plist)) return AnalysisState::kMissing;
  const std::optional<double> plist_time =
      platform::ModificationTime(util::pypath::Join({report_dir, plist}));
  const std::optional<double> source_time =
      platform::ModificationTime(source);
  // Unknown times count as up to date.
  if (plist_time && source_time && *plist_time <= *source_time) {
    return AnalysisState::kOutdated;
  }
  return AnalysisState::kUpToDate;
}

// Python: get_report_dir_status().
Result<ReportDirStatus> GetReportDirStatus(
    const std::vector<CompileCommand>& commands,
    const std::string& report_dir) {
  ReportDirStatus status;
  for (const AnalyzerDriver* analyzer : SupportedAnalyzers()) {
    status.analyzers.push_back(
        {.name = std::string(analyzer->name()), .files = {}});
  }
  const std::vector<std::string> names = platform::ListDirectory(report_dir);
  const std::set<std::string> files(names.begin(), names.end());
  std::set<std::string> analyzed;
  // The source files already listed, by analyzer (as in status.analyzers)
  // and state.
  std::vector<std::array<std::set<std::string>, kStateCount>> listed(
      status.analyzers.size());
  for (const CompileCommand& command : commands) {
    const Result<std::string> hash =
        AnalyzerActionHash(command.file, command.directory, command.command);
    if (!hash.ok()) return Result<ReportDirStatus>(hash.error());
    for (size_t i = 0; i < status.analyzers.size(); ++i) {
      AnalyzerStatus& analyzer = status.analyzers[i];
      const AnalysisState state = StateOf(
          command.file,
          std::format("{}_{}_{}.plist", util::pypath::Basename(command.file),
                      analyzer.name, hash.value()),
          report_dir, files);
      if (state != AnalysisState::kMissing) analyzed.insert(command.command);
      if (listed[i][Index(state)].insert(command.file).second) {
        analyzer.files[Index(state)].push_back(command.file);
      }
    }
  }
  status.analyzed_commands = static_cast<int>(analyzed.size());
  status.available_commands = static_cast<int>(commands.size());
  return Result<ReportDirStatus>(std::move(status));
}

// Logs `status` as INFO lines.
void LogStatus(const ReportDirStatus& status, bool detailed) {
  kLog.Info("----==== Summary ====----");
  for (const StateTitle& state_title : kStateTitles) {
    kLog.Info(state_title.title);
    for (const AnalyzerStatus& analyzer : status.analyzers) {
      const std::vector<std::string>& files =
          analyzer.files[Index(state_title.state)];
      if (files.empty()) continue;
      kLog.Info(detailed
                    ? std::format("  {}: {} ({})", analyzer.name,
                                  ListRepr(files), files.size())
                    : std::format("  {}: {}", analyzer.name, files.size()));
    }
  }
  kLog.Info(std::format("Total analyzed compilation commands: {}",
                        status.analyzed_commands));
  kLog.Info(std::format("Total available compilation commands: {}",
                        status.available_commands));
  kLog.Info("----=================----");
}

// Returns `status` as the JSON text Python's json.dump(indent=2) writes.
std::string StatusJson(const ReportDirStatus& status, bool detailed) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* analyzers = yyjson_mut_obj(doc);
  for (const AnalyzerStatus& analyzer : status.analyzers) {
    yyjson_mut_val* entry = yyjson_mut_obj(doc);
    yyjson_mut_val* summary = yyjson_mut_obj(doc);
    for (int state = 0; state < kStateCount; ++state) {
      yyjson_mut_obj_add(
          summary, util::MakeJsonString(doc, kStateKeys[state]),
          yyjson_mut_int(doc,
                         static_cast<int64_t>(analyzer.files[state].size())));
    }
    yyjson_mut_obj_add_val(doc, entry, "summary", summary);
    for (int state = 0; detailed && state < kStateCount; ++state) {
      yyjson_mut_val* files = yyjson_mut_arr(doc);
      for (const std::string& file : analyzer.files[state]) {
        yyjson_mut_arr_append(files, util::MakeJsonString(doc, file));
      }
      yyjson_mut_obj_add(entry, util::MakeJsonString(doc, kStateKeys[state]),
                         files);
    }
    yyjson_mut_obj_add(analyzers, util::MakeJsonString(doc, analyzer.name),
                       entry);
  }
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, root, "analyzers", analyzers);
  yyjson_mut_obj_add_int(doc, root, "total_analyzed_compilation_commands",
                         status.analyzed_commands);
  yyjson_mut_obj_add_int(doc, root, "total_available_compilation_commands",
                         status.available_commands);
  return util::DumpJsonIndent(root, 2);
}

// Prints the analysis status of the report folder `report_dir` (Python:
// print_status()): as log lines, or as JSON on stdout or into
// `output_path`. `files` limits it to some source files.
Result<void> PrintStatus(const std::string& report_dir, bool detailed,
                         const std::optional<std::vector<std::string>>& files,
                         const std::optional<std::string>& export_format,
                         const std::optional<std::string>& output_path) {
  if (export_format && *export_format != "json") {
    return Result<void>(
        Error{.message = "Only JSON export format is supported."});
  }
  if (output_path && !export_format) {
    return Result<void>(
        Error{.message = "Export format (--export/-e) was not specified."});
  }
  if (!util::pypath::IsDir(report_dir)) {
    return Result<void>(Error{.message = std::format(
                                  "Input path '{}' is not a directory.",
                                  report_dir)});
  }
  Result<std::vector<CompileCommand>> commands =
      LoadCompileCommands(report_dir);
  if (!commands.ok()) return Result<void>(commands.error());
  if (files) {
    std::vector<std::string> filter;
    std::vector<std::string> invalid;
    for (const std::string& file : *files) {
      filter.push_back(util::pypath::Abspath(file));
      if (!util::pypath::IsFile(filter.back())) {
        invalid.push_back(filter.back());
      }
    }
    if (!invalid.empty()) {
      kLog.Error("File filter (--file) contains nonexistent files:");
      return Result<void>(Error{.message = ListRepr(invalid)});
    }
    std::erase_if(commands.value(), [&filter](const CompileCommand& command) {
      return std::ranges::find(filter, command.file) == filter.end();
    });
    if (commands.value().empty() && !export_format) {
      kLog.Warning("File not found in the compilation database!");
    }
  }
  const Result<ReportDirStatus> status =
      GetReportDirStatus(commands.value(), report_dir);
  if (!status.ok()) return Result<void>(status.error());
  if (!export_format) {
    LogStatus(status.value(), detailed);
  } else if (!output_path) {
    util::Write(util::Stream::kStdout, StatusJson(status.value(), detailed));
  } else {
    const std::string path = util::pypath::Abspath(*output_path);
    if (const Result<void> written =
            util::WriteFile(path, StatusJson(status.value(), detailed));
        !written.ok()) {
      // Python ends with an uncaught OSError here.
      return Result<void>(Uncaught(written.error()));
    }
    kLog.Info(std::format("Status info was dumped to '{}'.", path));
  }
  return Result<void>();
}

// --- Printing the reports ---

// What `parse` uses and collects while it reads the result files.
struct ParseState {
  // The labels of the analyzer context, which outlives the run.
  const common::CheckerLabels* checker_labels = nullptr;
  common::SkipListHandlers skip_handlers;
  std::optional<SuppressHandler> suppress_handler;
  // The review statuses to show; empty shows all.
  std::vector<std::string> review_status_filter;
  common::ReviewStatusHandler review_status_handler;
  report::Statistics statistics;
  std::set<std::string> changed_files;
  std::set<std::string> processed_path_hashes;
  std::set<std::string> processed_file_paths;
  // The patterns of --trim-path-prefix; empty: the paths are not trimmed.
  std::vector<util::Regex> trim_patterns;
  report::TrimmedPaths trimmed_paths;
  // The absolute path of -o: a folder for HTML, else a file.
  std::optional<std::string> output_path;
  // The HTML pages; only for --export html.
  std::optional<report::HtmlBuilder> html_builder;
  // The shown reports, for the exports other than HTML.
  std::vector<report::Report> exported_reports;
};

// Returns the reports of `reports` that are shown (Python:
// reports_helper.skip()): not skipped, not suppressed, with a review status
// that the filter shows, and not a duplicate of a report seen before. With
// --export-source-suppress, the review statuses of the reports are written
// to the suppress file.
std::vector<report::Report> KeptReports(std::vector<report::Report> reports,
                                        ParseState* state) {
  std::vector<report::Report> kept;
  for (report::Report& report : reports) {
    if (common::ShouldSkip(state->skip_handlers, report.file.original_path)) {
      kConverterLog.Debug("Skip report because file path (" +
                          report.file.path + ") is on the skip list.");
      continue;
    }
    const std::string& status = report.review_status.status;
    if (state->suppress_handler) {
      if (state->suppress_handler->IsSuppressed(report)) {
        kConverterLog.Debug("Suppressed by suppress file: " +
                            report.file.original_path);
        continue;
      }
      if (report.report_hash.value_or("").empty()) {
        kConverterLog.Warning(
            "Can't store suppress information for report because no report "
            "hash is set: " +
            report::ReportRepr(report));
        continue;
      }
      if (status != "unreviewed") {
        state->suppress_handler->StoreSuppressBugId(
            *report.report_hash,
            util::pypath::Basename(report.file.original_path),
            report.review_status.message, status);
      }
    }
    const std::vector<std::string>& filter = state->review_status_filter;
    if (!filter.empty() && std::ranges::find(filter, status) == filter.end()) {
      kConverterLog.Debug("Filtered out by --review-status filter option: " +
                          report.file.original_path);
      continue;
    }
    if (!state->processed_path_hashes.insert(report::GetReportPathHash(report))
             .second) {
      kConverterLog.Debug(
          "Not showing report because it is a deduplication of an already "
          "processed report!");
      continue;
    }
    kept.push_back(std::move(report));
  }
  return kept;
}

// Reads and filters the reports of the result file at `path`, and prints
// them, writes their HTML page or keeps them for the export. `source_files`
// maps result files to the source files they were made from.
Result<void> ProcessResultFile(
    const std::string& path, const ParseArgs& args,
    const std::unordered_map<std::string, std::string>& source_files,
    ParseState* state) {
  const bool trim = !state->trim_patterns.empty();
  std::vector<report::Report> reports = report::GetReports(path);
  for (report::Report& report : reports) {
    report.severity = state->checker_labels->UniqueLabelOfChecker(
        report.checker_name, "severity", std::nullopt);
    if (trim) report::ApplyTrimmedPaths(state->trimmed_paths, &report);
    // Only the comments of the reports that are shown are read, as in
    // Python, because an ambiguous comment ends the command.
    if (common::ShouldSkip(state->skip_handlers, report.file.original_path)) {
      continue;
    }
    Result<report::ReviewStatus> status =
        state->review_status_handler.GetReviewStatus(report);
    if (!status.ok()) return Result<void>(status.error());
    report.review_status = std::move(status).value();
  }
  std::vector<report::Report> kept = KeptReports(std::move(reports), state);
  state->statistics.AddResultFile();
  for (report::Report& report : kept) {
    state->changed_files.merge(report::ChangedFiles(report));
    state->statistics.AddReport(report);
    if (trim) {
      report::TrimPaths(state->trim_patterns, &report,
                        &state->trimmed_paths);
    }
  }
  if (trim) {
    for (report::Report& report : kept) {
      report::ApplyTrimmedPaths(state->trimmed_paths, &report);
    }
  }
  if (!args.export_format) {
    const auto source_file = source_files.find(path);
    report::PrintReports(
        report::GetFileReportMap(
            kept, source_file != source_files.end()
                      ? std::optional<std::string>(source_file->second)
                      : std::nullopt),
        args.print_steps, &state->processed_file_paths);
  } else if (state->html_builder) {
    util::Print(std::format("Parsing input file '{}'.", path));
    if (Result<void> converted =
            state->html_builder->Convert(path, kept, *state->output_path);
        !converted.ok()) {
      return Result<void>(Uncaught(converted.error()));
    }
  } else {
    std::ranges::move(kept, std::back_inserter(state->exported_reports));
  }
  return Result<void>();
}

// Returns the result files of the version 2 metadata `metadata` whose
// source file matches one of the --file `patterns`; only the first tool is
// looked at, as in Python.
std::vector<std::string> ResultFilesOfSources(
    yyjson_val* metadata, const std::vector<std::string>& patterns) {
  std::vector<std::string> result_files;
  yyjson_val* tools = util::JsonGet(metadata, "tools");
  yyjson_val* source_files =
      util::JsonGet(yyjson_arr_get_first(tools), "result_source_files");
  yyjson_obj_iter iter = yyjson_obj_iter_with(source_files);
  while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
    const std::string source =
        util::JsonToStr(yyjson_obj_iter_get_val(key));
    if (std::ranges::any_of(patterns, [&source](const std::string& pattern) {
          return util::FnMatch(source, pattern);
        })) {
      result_files.push_back(util::JsonToStr(key));
    }
  }
  return result_files;
}

// Processes the result files of `folder`, with the review status rules and
// the metadata of the folder.
Result<void> ProcessFolder(const report::ResultFolder& folder,
                           const ParseArgs& args, ParseState* state) {
  const std::string rules_file =
      util::pypath::Join({folder.path, "review_status.yaml"});
  if (util::pypath::Exists(rules_file) || util::pypath::IsLink(rules_file)) {
    if (Result<void> loaded =
            state->review_status_handler.SetReviewStatusConfig(rules_file);
        !loaded.ok()) {
      return loaded;
    }
  }
  const std::string metadata_file =
      util::pypath::Join({folder.path, "metadata.json"});
  std::optional<util::JsonDocument> document;
  if (util::pypath::Exists(metadata_file)) {
    document = common::LoadJson(metadata_file);
  }
  yyjson_val* metadata =
      document ? yyjson_doc_get_root(document->get()) : nullptr;
  // Python takes only a non-empty object as metadata.
  if (!yyjson_is_obj(metadata) || yyjson_obj_size(metadata) == 0) {
    metadata = nullptr;
  }
  std::vector<std::string> files = folder.files;
  if (metadata != nullptr && args.files) {
    // The result files are looked up by their source files.
    if (std::vector<std::string> specified =
            ResultFilesOfSources(metadata, *args.files);
        !specified.empty()) {
      files = std::move(specified);
    }
  }
  std::unordered_map<std::string, std::string> source_files;
  if (metadata != nullptr) {
    for (auto& [result_file, source] : ResultSourceFiles(metadata)) {
      source_files.emplace(std::move(result_file), std::move(source));
    }
  }
  for (const std::string& file : files) {
    if (Result<void> processed =
            ProcessResultFile(file, args, source_files, state);
        !processed.ok()) {
      return processed;
    }
  }
  return Result<void>();
}

// Returns the suppress handler of --suppress, if there is one: the file is
// created for --export-source-suppress if it does not exist.
Result<std::optional<SuppressHandler>> MakeSuppressHandler(
    const ParseArgs& args, const std::vector<std::string>& filter) {
  using Handler = Result<std::optional<SuppressHandler>>;
  if (!args.suppress) {
    if (args.export_source_suppress) {
      return Handler(Error{.message = "Can't use '--export-source-suppress' "
                                      "unless '--suppress SUPPRESS_FILE' is "
                                      "also given."});
    }
    return Handler(std::nullopt);
  }
  const std::string& path = *args.suppress;
  if (!util::pypath::IsFile(path)) {
    if (!args.export_source_suppress) {
      kLog.Warning(std::format("Suppress file '{}' given, but it does not "
                               "exist -- will not suppress anything.",
                               path));
      return Handler(std::nullopt);
    }
    if (const Result<void> created = util::WriteFile(path, "");
        !created.ok()) {
      // Python ends with an uncaught OSError here.
      return Handler(Uncaught(created.error()));
    }
    kLog.Info("Will write source-code suppressions to suppress file: " +
              path);
  }
  Result<SuppressHandler> handler =
      SuppressHandler::Create(path, args.export_source_suppress, filter);
  if (!handler.ok()) {
    return Handler(Uncaught(handler.error()));
  }
  return Handler(std::move(handler).value());
}

// Returns the -o path, made absolute, after checking it, and creates the
// folder the export is written into: the path itself for HTML, else its
// parent. Without an export format nothing is written, but Python checks
// the path all the same.
Result<std::optional<std::string>> PrepareOutputPath(const ParseArgs& args) {
  using Path = Result<std::optional<std::string>>;
  if (!args.output_path) return Path(std::nullopt);
  const std::string path = util::pypath::Abspath(*args.output_path);
  std::string dir = path;
  if (args.export_format != "html") {
    if (util::pypath::IsDir(path)) {
      return Path(Error{
          .message = std::format("Please provide a file path instead of a "
                                 "directory for '{}' export type!",
                                 args.export_format.value_or("None"))});
    }
    if (args.export_format == "baseline" && !report::IsBaselineFile(path)) {
      return Path(
          Error{.message = "Baseline files must have '.baseline' extensions."});
    }
    dir = util::pypath::Dirname(path);
  }
  if (!util::pypath::Exists(dir)) {
    if (const std::error_code error = platform::MakeDirs(dir)) {
      // Python ends with an uncaught OSError here.
      return Path(Uncaught(util::OsError(error.value(), dir)));
    }
  }
  return Path(path);
}

// Returns the skip handlers of --file and -i.
Result<common::SkipListHandlers> MakeSkipHandlers(const ParseArgs& args) {
  common::SkipListHandlers handlers;
  if (args.files) {
    std::vector<std::string> lines;
    for (const std::string& file : *args.files) lines.push_back("+" + file);
    lines.push_back("-*");
    handlers.emplace_back(util::Join(lines, "\n"));
  }
  if (args.skipfile) {
    const Result<std::string> content = util::ReadTextFile(*args.skipfile);
    if (!content.ok()) {
      // Python ends with an uncaught OSError here.
      return Result<common::SkipListHandlers>(Uncaught(content.error()));
    }
    handlers.emplace_back(content.value());
  }
  return Result<common::SkipListHandlers>(std::move(handlers));
}

// Returns the lookup of the checker labels that the HTML and SARIF
// exports show, in `labels`, which must outlive it.
report::CheckerInfoLookup CheckerInfoOf(const common::CheckerLabels& labels) {
  return [&labels](const std::string& checker,
                   const std::optional<std::string>& analyzer) {
    const std::optional<std::string_view> analyzer_name =
        analyzer ? std::optional<std::string_view>(*analyzer) : std::nullopt;
    const std::vector<std::string> doc_urls =
        labels.LabelsOfChecker(checker, "doc_url", analyzer_name);
    return report::CheckerInfo{
        .severity =
            labels.UniqueLabelOfChecker(checker, "severity", analyzer_name),
        .doc_url = doc_urls.empty() ? std::nullopt
                                    : std::optional<std::string>(doc_urls[0])};
  };
}

// Writes the export of `args` other than HTML: the shown reports, kept in
// `state`, into the -o file or on stdout.
Result<void> ExportReports(const ParseArgs& args,
                           const report::CheckerInfoLookup& checker_info,
                           ParseState* state) {
  std::vector<report::Report>& reports = state->exported_reports;
  // The reports read first see the paths trimmed for the later ones, as
  // they do through Python's shared File objects.
  for (report::Report& report : reports) {
    report::ApplyTrimmedPaths(state->trimmed_paths, &report);
  }
  const std::string& format = *args.export_format;
  if (format == "baseline") {
    // Without -o, nothing is written.
    if (!state->output_path) return Result<void>();
    if (Result<void> written = report::WriteBaseline(
            *state->output_path, report::BaselineHashes(reports));
        !written.ok()) {
      return Result<void>(Uncaught(written.error()));
    }
    return Result<void>();
  }
  std::string json;
  if (format == "json") {
    json = report::ReportsToJson(reports);
  } else if (format == "codeclimate") {
    json = report::ReportsToCodeClimate(reports);
  } else if (format == "sarif") {
    json = report::ReportsToSarif(reports, checker_info);
  } else {  // gerrit
    Result<std::string> review = report::ReportsToGerrit(reports);
    if (!review.ok()) return Result<void>(Uncaught(review.error()));
    json = std::move(review).value();
  }
  if (Result<void> dumped = report::DumpJsonOutput(json, state->output_path);
      !dumped.ok()) {
    return Result<void>(Uncaught(dumped.error()));
  }
  return Result<void>();
}

// Fills `state` from `args` before the inputs are read: the review status
// filter, the suppress and skip handlers, the checked -o path and, for
// --export html, the HTML builder, which uses `checker_info`.
Result<void> PrepareParseState(const ParseArgs& args,
                               const AnalyzerContext& context,
                               const report::CheckerInfoLookup& checker_info,
                               ParseState* state) {
  state->checker_labels = &context.checker_labels();
  std::vector<std::string> default_filter = {"confirmed", "unreviewed"};
  if (args.export_format == "sarif") {
    default_filter.assign(std::begin(common::kReviewStatusValues),
                          std::end(common::kReviewStatusValues));
  }
  state->review_status_filter = args.review_status.value_or(default_filter);
  state->trim_patterns = report::PathPrefixPatterns(args.trim_path_prefixes);
  Result<std::optional<SuppressHandler>> suppress_handler =
      MakeSuppressHandler(args, state->review_status_filter);
  if (!suppress_handler.ok()) return Result<void>(suppress_handler.error());
  state->suppress_handler = std::move(suppress_handler).value();
  Result<std::optional<std::string>> output_path = PrepareOutputPath(args);
  if (!output_path.ok()) return Result<void>(output_path.error());
  state->output_path = std::move(output_path).value();
  Result<common::SkipListHandlers> skip_handlers = MakeSkipHandlers(args);
  if (!skip_handlers.ok()) return Result<void>(skip_handlers.error());
  state->skip_handlers = std::move(skip_handlers).value();
  if (args.export_format == "html") {
    Result<report::HtmlBuilder> builder = report::HtmlBuilder::Create(
        util::pypath::Join(
            {context.data_files_dir(), "lib", "codechecker", "html"}),
        checker_info);
    if (!builder.ok()) return Result<void>(Uncaught(builder.error()));
    state->html_builder = std::move(builder).value();
  }
  return Result<void>();
}

// Writes the result of the reports read into `state`: the statistics
// without an export format, else the export of `args`.
Result<void> WriteParseOutput(const ParseArgs& args,
                              const report::CheckerInfoLookup& checker_info,
                              ParseState* state) {
  if (!args.export_format) {
    state->statistics.Write();
    return Result<void>();
  }
  if (!state->html_builder) return ExportReports(args, checker_info, state);
  if (Result<void> finished =
          state->html_builder->Finish(*state->output_path, state->statistics);
      !finished.ok()) {
    return Result<void>(Uncaught(finished.error()));
  }
  return Result<void>();
}

// Prints or exports the reports of the inputs, prints the analysis status
// of the first input, and returns the exit code.
Result<int> ParseReports(const ParseArgs& args,
                         const AnalyzerContext& context) {
  const report::CheckerInfoLookup checker_info =
      CheckerInfoOf(context.checker_labels());
  ParseState state;
  if (Result<void> prepared =
          PrepareParseState(args, context, checker_info, &state);
      !prepared.ok()) {
    return Result<int>(prepared.error());
  }
  for (const report::ResultFolder& folder :
       report::AnalyzerResultFiles(args.inputs)) {
    if (Result<void> processed = ProcessFolder(folder, args, &state);
        !processed.ok()) {
      return Result<int>(processed.error());
    }
  }
  for (const std::string& warning :
       state.review_status_handler.source_comment_warnings()) {
    kLog.Warning(warning);
  }
  if (Result<void> written = WriteParseOutput(args, checker_info, &state);
      !written.ok()) {
    return Result<int>(written.error());
  }
  report::DumpChangedFiles(state.changed_files);
  const std::string& input = args.inputs.front();
  if (util::pypath::IsDir(input) &&
      util::pypath::IsFile(util::pypath::Join({input, "compile_cmd.json"}))) {
    if (Result<void> printed = PrintStatus(input, /*detailed=*/false,
                                           std::nullopt, std::nullopt,
                                           std::nullopt);
        !printed.ok()) {
      return Result<int>(printed.error());
    }
  }
  return Result<int>(state.statistics.num_of_reports() > 0 ? 2 : 0);
}

// Python: main() of parse.py, after the logger is set up.
Result<int> Parse(const argparse::Namespace& ns) {
  if (Result<void> checked = common::CheckConfigFile(ns); !checked.ok()) {
    return Result<int>(checked.error());
  }
  const ParseArgs args = ToParseArgs(ns);
  bool inputs_exist = true;
  for (const std::string& input : args.inputs) {
    if (!util::pypath::Exists(input)) {
      kLog.Error(std::format("Input path {} does not exist!", input));
      inputs_exist = false;
    }
  }
  if (!inputs_exist) return Result<int>(Error{});
  if (args.export_format == "html" && !args.output_path) {
    return Result<int>(Error{.message = "Argument --export not allowed "
                                        "without argument --output when "
                                        "exporting to HTML."});
  }
  if (args.export_format == "gerrit" && !report::GerritEnvironmentIsSet()) {
    return Result<int>(Error{});
  }
  const Result<AnalyzerContext*> context = GetContext();
  if (!context.ok()) return Result<int>(context.error());
  if (args.status) {
    if (Result<void> printed =
            PrintStatus(args.inputs.front(), args.detailed, args.files,
                        args.export_format, args.output_path);
        !printed.ok()) {
      return Result<int>(printed.error());
    }
    return Result<int>(0);
  }
  return ParseReports(args, *context.value());
}

}  // namespace

void AddSuppressArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"--suppress"},
       .dest = "suppress",
       .help = "Path of the suppress file to use. Records in the suppress "
               "file are used to suppress the display of certain results when "
               "parsing the analyses' report. (Reports to an analysis result "
               "can also be suppressed in the source code -- please consult "
               "the manual on how to do so.) NOTE: The suppress file relies on "
               "the \"bug identifier\" generated by the analyzers which is "
               "experimental, take care when relying on it.",
       .group = group});
}

void AddPrintStepsArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"--print-steps"},
       .dest = "print_steps",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Print the steps the analyzers took in finding the reported "
               "defect.",
       .group = group});
}

void AddTrimPathPrefixArgument(argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"--trim-path-prefix"},
       .dest = "trim_path_prefix",
       .nargs = argparse::Nargs::kZeroOrMore,
       .help = "Removes leading path from files which will be printed. For "
               "instance if you analyze files '/home/jsmith/my_proj/x.cpp' "
               "and '/home/jsmith/my_proj/y.cpp', but would prefer to have "
               "them displayed as 'my_proj/x.cpp' and 'my_proj/y.cpp' in the "
               "web/CLI interface, invoke CodeChecker with "
               "'--trim-path-prefix \"/home/jsmith/\"'.If multiple prefixes "
               "are given, the longest match will be removed. You may also "
               "use Unix shell-like wildcards (e.g. '/*/jsmith/').",
       .group = group});
}

void AddParseArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));
  parser.set_epilog(
      "\nEnvironment variables\n"
      "------------------------------------------------\n" +
      std::string(kParseEnvironmentVariables) + std::string(kEpilogExitStatus));

  parser.AddArgument(
      {.dest = "input",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "file/folder",
       .help = "The analysis result files and/or folders containing analysis "
               "results which should be parsed and printed."});
  common::AddConfigOption(parser, /*group=*/0);
  parser.AddArgument(
      {.flags = {"-t", "--type", "--input-format"},
       .dest = "input_format",
       .choices = {"plist"},
       .default_value = "plist",
       .help = "Specify the format the analysis results were created as."});

  const int export_group = parser.AddArgumentGroup("export arguments", "");
  parser.AddArgument(
      {.flags = {"-e", "--export"},
       .dest = "export",
       .choices = std::vector<std::string>(std::begin(kExportTypes),
                                           std::end(kExportTypes)),
       .default_none = true,
       .help = "R|Specify extra output format type.\n'codeclimate' format can "
               "be used for Code Climate and for GitLab integration. For more "
               "information see:\nhttps://github.com/codeclimate/platform/"
               "blob/master/spec/analyzers/SPEC.md#data-types\n'baseline' "
               "output can be used to integrate CodeChecker into your local "
               "workflow without using a CodeChecker server. For more "
               "information see our usage guide.",
       .group = export_group});
  parser.AddArgument(
      {.flags = {"-o", "--output"},
       .dest = "output_path",
       .help = "Store the output in the given file/folder. Note: baseline "
               "files must have extension '.baseline'.",
       .group = export_group});

  AddSuppressArgument(parser, /*group=*/0);
  parser.AddArgument(
      {.flags = {"--export-source-suppress"},
       .dest = "create_suppress",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Write suppress data from the suppression annotations found in "
               "the source files that were analyzed earlier that created the "
               "results. The suppression information will be written to the "
               "parameter of '--suppress'."});
  AddPrintStepsArgument(parser, /*group=*/0);
  AddTrimPathPrefixArgument(parser, /*group=*/0);
  std::vector<std::string> review_statuses(
      std::begin(common::kReviewStatusValues),
      std::end(common::kReviewStatusValues));
  parser.AddArgument(
      {.flags = {"--review-status"},
       .dest = "review_status",
       .nargs = argparse::Nargs::kZeroOrMore,
       .metavar = "REVIEW_STATUS",
       .choices = review_statuses,
       .help = "Filter results by review statuses. Valid values are: " +
               util::Join(review_statuses, ", ") +
               " (default: everything for SARIF export, otherwise "
               "['confirmed', 'unreviewed'])"});
  parser.AddArgument(
      {.flags = {"--status"},
       .dest = "status",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Print the number of successful and failed analysis "
               "actions."});
  parser.AddArgument(
      {.flags = {"--detailed"},
       .dest = "detailed",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Enables detailed view for the status command."});

  const int filter_group =
      parser.AddArgumentGroup("file filter arguments", "");
  parser.AddArgument(
      {.flags = {"-i", "--ignore", "--skip"},
       .dest = "skipfile",
       .help = "Path to the Skipfile dictating which project files should be "
               "omitted from analysis. Please consult the User guide on how a "
               "Skipfile should be laid out.",
       .group = filter_group});
  parser.AddArgument(
      {.flags = {"--file"},
       .dest = "files",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "FILE",
       .help = "Filter results by file path. The file path can contain "
               "multiple * quantifiers which matches any number of characters "
               "(zero or more). So if you have /a/x.cpp and /a/y.cpp then "
               "\"/a/*.cpp\" selects both.",
       .group = filter_group});
  common::cli::AddVerboseArgument(parser);
}

int RunParse(const argparse::Namespace& args) {
  // Exported data goes to stdout, so the log goes to stderr then.
  const bool to_stderr = args.Has("export") && args.Get("export") != "html";
  util::SetupLogger(common::cli::Verbose(args), to_stderr
                                                    ? util::Stream::kStderr
                                                    : util::Stream::kStdout);
  const Result<int> result = Parse(args);
  if (result.ok()) return result.value();
  if (!result.error().message.empty()) kLog.Error(result.error().message);
  return result.error().exit_code;
}

}  // namespace cc::analyzer::cli
