// Port of codechecker_report_converter/cli.py and the transform() of
// analyzers/analyzer_result.py: the report-converter program
// (report-converter.md).

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "platform/process.h"
#include "report/converter/analyzer_types.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/output/plist.h"
#include "report/output/sarif.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::tools {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("report-converter");

constexpr std::string_view kDescription =
    "\n"
    "Creates a CodeChecker report directory from the given code analyzer "
    "output\n"
    "which can be stored to a CodeChecker web server.";

constexpr std::string_view kFilenameHelp =
    "This option can be used to override the default plist file name output "
    "of this tool. This tool can produce multiple plist files on the given "
    "code analyzer output result file. The problem is if we run this tool "
    "multiple times on the same file, it may override some plist files. To "
    "prevent this we can generate a unique hash into the plist file names "
    "with this option. For example: "
    "'{source_file}_{analyzer}_{file_hash}_xxxxx'. {source_file}, {analyzer} "
    "and {file_hash} are special values which will be replaced with the "
    "current analyzer, source file name and hash of the absolute file path "
    "where the bug was found. ";

// The metadata keys that --meta accepts.
constexpr std::string_view kMetadataKeys[] = {"analyzer_command",
                                              "analyzer_version"};

// Returns the names of the types, sorted, as the help texts list them.
std::vector<std::string> SortedTypeNames() {
  std::vector<std::string> names;
  for (const report::AnalyzerType& type : report::AnalyzerTypes()) {
    names.emplace_back(type.tool_name);
  }
  std::ranges::stable_sort(names);
  return names;
}

// Prints how to produce input for a type, then ends the parse with exit
// code 0 (Python: PrintExampleCmdAction).
Result<std::vector<std::string>> PrintExample(
    const std::vector<std::string>& values,
    const std::vector<std::string>* /*previous*/) {
  const report::AnalyzerType& type = *report::FindAnalyzerType(values[0]);
  if (type.example_command.empty()) {
    kLog.Warning("No usage example is available yet for '" + values[0] +
                 "'. Please see docs/tools/report-converter.md for more "
                 "information.");
  } else {
    util::Print("Example commands to produce a '" + values[0] +
                "' output which 'report-converter' can parse:\n");
    util::Print(type.example_command);
  }
  util::Flush(util::Stream::kStdout);
  return Result<std::vector<std::string>>(Error{.exit_code = 0});
}

// Returns every file of the given files and folders (Python: CollectFiles).
// Missing paths are logged and end the program with exit code 1.
Result<std::vector<std::string>> CollectFiles(
    const std::vector<std::string>& paths,
    const std::vector<std::string>* /*previous*/) {
  using Files = Result<std::vector<std::string>>;
  std::vector<std::string> files;
  bool missing = false;
  for (const std::string& path : paths) {
    if (!util::pypath::Exists(path)) {
      kLog.Error("File or directory '" + path + "' does not exist!");
      missing = true;
    } else if (util::pypath::IsFile(path)) {
      files.push_back(path);
    } else {
      std::vector<std::string> found = platform::WalkFiles(path);
      files.insert(files.end(), found.begin(), found.end());
    }
  }
  if (missing) return Files(Error{.exit_code = 1});
  // Python asserts that there is an input file.
  if (files.empty()) return Files(Error{.message = "AssertionError"});
  return Files(std::move(files));
}

void AddArguments(argparse::Parser& parser) {
  std::vector<std::string> type_names;
  for (const report::AnalyzerType& type : report::AnalyzerTypes()) {
    type_names.emplace_back(type.tool_name);
  }
  const std::string sorted_names = util::Join(SortedTypeNames(), ", ");
  parser.AddArgument(
      {.flags = {"--example"},
       .dest = "example",
       .metavar = "TYPE",
       .choices = type_names,
       .help = "Print an example command showing how to produce an analyzer "
               "result file of the given TYPE that 'report-converter' can "
               "parse, then exit. Currently supported types are: " +
               sorted_names + ".",
       .custom_action = PrintExample});
  parser.AddArgument(
      {.dest = "input",
       .nargs = argparse::Nargs::kOneOrMore,
       .help = "Code analyzer output result files or directories which will "
               "be parsed and used to generate a CodeChecker report "
               "directory.",
       .custom_action = CollectFiles});
  parser.AddArgument({.flags = {"-o", "--output"},
                      .dest = "output_dir",
                      .required = true,
                      .help = "This directory will be used to generate "
                              "CodeChecker report directory files."});
  parser.AddArgument(
      {.flags = {"-t", "--type"},
       .dest = "type",
       .metavar = "TYPE",
       .choices = type_names,
       .required = true,
       .help = "Specify the format of the code analyzer output. Currently "
               "supported output types are: " +
               sorted_names + "."});
  parser.AddArgument({.flags = {"-e", "--export"},
                      .dest = "export",
                      .metavar = "EXPORT",
                      .choices = {"plist", "sarif"},
                      .default_value = "plist",
                      .help = "Specify the export format of the converted "
                              "reports. Currently supported export types "
                              "are: .plist, .sarif."});
  parser.AddArgument(
      {.flags = {"--meta"},
       .dest = "meta",
       .nargs = argparse::Nargs::kZeroOrMore,
       .metavar = "META",
       .default_none = true,
       .help = "Metadata information which will be stored alongside the run "
               "when the created report directory will be stored to a "
               "running CodeChecker server. It has the following format: "
               "key=value. Valid key values are: analyzer_command, "
               "analyzer_version."});
  parser.AddArgument({.flags = {"--filename"},
                      .dest = "filename",
                      .metavar = "FILENAME",
                      .default_value = "{source_file}_{analyzer}_{file_hash}",
                      .help = std::string(kFilenameHelp)});
  parser.AddArgument({.flags = {"-c", "--clean"},
                      .dest = "clean",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .default_value = "False",
                      .help = "Delete files stored in the output directory."});
  parser.AddArgument({.flags = {"-v", "--verbose"},
                      .dest = "verbose",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .default_value = "False",
                      .help = "Set verbosity level."});
}

// Returns `text` with every `from` replaced by `to` (Python: str.replace()).
std::string ReplaceAll(std::string text, std::string_view from,
                       std::string_view to) {
  for (size_t pos = text.find(from); pos != std::string::npos;
       pos = text.find(from, pos + to.size())) {
    text.replace(pos, from.size(), to);
  }
  return text;
}

// Returns the reports as Python's repr() of a list of Report objects shows
// them.
std::string ReportsRepr(const std::vector<report::Report>& reports) {
  std::vector<std::string> items;
  for (const report::Report& report : reports) {
    const util::MutableJsonDocument document = util::NewJsonDocument();
    items.push_back(
        util::DumpJson(report::ReportToJson(report, document.get())));
  }
  return "[" + util::Join(items, ", ") + "]";
}

// Writes the reports into one result file per source file (Python:
// AnalyzerResultBase._write()).
Result<void> WriteResultFiles(const std::vector<report::Report>& reports,
                              const std::string& output_dir,
                              std::string_view tool_name,
                              const std::string& export_type,
                              const std::string& file_name) {
  // The reports of each source file, in the order the files first appear.
  std::vector<std::pair<std::string, std::vector<report::Report>>> groups;
  for (const report::Report& report : reports) {
    const std::string path = util::pypath::Normpath(report.file.original_path);
    auto group = std::ranges::find(
        groups, path,
        &std::pair<std::string, std::vector<report::Report>>::first);
    if (group == groups.end()) {
      groups.emplace_back(path, std::vector<report::Report>());
      group = groups.end() - 1;
    }
    group->second.push_back(report);
  }
  const std::string output = util::pypath::Abspath(output_dir);
  for (const auto& [path, file_reports] : groups) {
    std::string name = ReplaceAll(file_name, "{source_file}",
                                  util::pypath::Basename(path));
    name = ReplaceAll(name, "{analyzer}", tool_name);
    name = ReplaceAll(name, "{file_hash}", util::Md5Hex(path));
    const std::string out_path =
        util::pypath::Join({output, name + "." + export_type});
    kLog.Info("Create/modify " + export_type + " file: '" + out_path + "'.");
    if (util::CurrentLogLevel() <= util::LogLevel::kDebug) {
      kLog.Debug(ReportsRepr(file_reports));
    }
    Result<void> written =
        export_type == "sarif"
            ? report::WriteSarifFile(out_path, file_reports)
            : report::WritePlistFile(out_path, file_reports,
                                     std::string(tool_name));
    if (!written.ok()) return written;
  }
  return Result<void>();
}

// Writes metadata.json with the --meta values (Python: _save_metadata()).
Result<void> SaveMetadata(
    const std::vector<std::pair<std::string, std::string>>& metadata,
    std::string_view tool_name, const std::string& output_dir) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* tool = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_strncpy(doc, tool, "name", tool_name.data(),
                             tool_name.size());
  for (const auto& [key, field] :
       {std::pair<std::string_view, const char*>("analyzer_version",
                                                 "version"),
        std::pair<std::string_view, const char*>("analyzer_command",
                                                 "command")}) {
    const auto value = std::ranges::find(
        metadata, key, &std::pair<std::string, std::string>::first);
    if (value != metadata.end()) {
      yyjson_mut_obj_add_strncpy(doc, tool, field, value->second.data(),
                                 value->second.size());
    }
  }
  yyjson_mut_val* tools = yyjson_mut_arr(doc);
  yyjson_mut_arr_append(tools, tool);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, root, "version", 2);
  yyjson_mut_obj_add_int(doc, root, "num_of_report_dir", 1);
  yyjson_mut_obj_add_val(doc, root, "tools", tools);
  return util::WriteFile(util::pypath::Join({output_dir, "metadata.json"}),
                         util::DumpJson(root));
}

// Converts the inputs into the output folder (Python: transform_output()
// and AnalyzerResultBase.transform()).
Result<void> Transform(
    const argparse::Namespace& args,
    const std::vector<std::pair<std::string, std::string>>& metadata) {
  const report::AnalyzerType& type =
      *report::FindAnalyzerType(args.Get("type"));
  const std::string& output_dir = args.Get("output_dir");
  if (args.Get("clean") == "True" && util::pypath::IsDir(output_dir)) {
    kLog.Info("Previous analysis results in '" + output_dir +
              "' have been removed, overwriting with current result");
    if (const std::error_code error = platform::RemoveTree(output_dir)) {
      return Result<void>(util::OsError(error.value(), output_dir));
    }
  }
  if (!util::pypath::Exists(output_dir)) {
    if (const std::error_code error = platform::MakeDirs(output_dir)) {
      return Result<void>(util::OsError(error.value(), output_dir));
    }
  }

  std::vector<report::Report> all_reports;
  for (const std::string& input : args.GetList("input")) {
    const std::string path = util::pypath::Abspath(input);
    Result<std::vector<report::Report>> reports = type.get_reports(path);
    if (!reports.ok()) return Result<void>(reports.error());
    if (reports.value().empty()) {
      kLog.Info("No '" + std::string(type.tool_name) +
                "' results can be found in '" + path + "'.");
    }
    for (report::Report report : std::move(reports).value()) {
      report.report_hash = report::GetReportHash(report, type.hash_type);
      report.analyzer_name = std::string(type.tool_name);
      report.analyzer_result_file_path = path;
      if (report.checker_name.empty()) {
        report.checker_name = std::string(type.tool_name);
      }
      all_reports.push_back(std::move(report));
    }
  }

  if (Result<void> written =
          WriteResultFiles(all_reports, output_dir, type.tool_name,
                           args.Get("export"), args.Get("filename"));
      !written.ok()) {
    return written;
  }
  if (!metadata.empty()) return SaveMetadata(metadata, type.tool_name,
                                             output_dir);
  kLog.Warning(
      "Use '--meta' option to provide extra information to the CodeChecker "
      "server such as analyzer version and analysis command when storing "
      "the results to it. For more information see the --help.");
  return Result<void>();
}

// Returns the exit code for `error` after printing its message, if it has
// one, on stderr, as the last line of a Python traceback would be.
int Fail(const Error& error) {
  if (!error.message.empty()) {
    util::Flush(util::Stream::kStdout);
    util::Write(util::Stream::kStderr, error.message + "\n");
  }
  return error.exit_code;
}

int Run(const std::vector<std::string>& arguments) {
  util::SetupToolLogger(util::LogLevel::kInfo);
  std::string epilog = "\nSupported analyzers:";
  for (const std::string& name : SortedTypeNames()) {
    const report::AnalyzerType& type = *report::FindAnalyzerType(name);
    epilog += "\n  " + name + " - " + std::string(type.name) + ", " +
              std::string(type.url);
  }
  argparse::Parser parser("report-converter");
  parser.set_description(std::string(kDescription));
  parser.set_epilog(std::move(epilog));
  parser.set_help_format(
      argparse::HelpFormat::kRawDescriptionArgumentDefaults);
  parser.set_error_exit_code(2);
  AddArguments(parser);
  const Result<argparse::Namespace> parsed = parser.ParseArgs(arguments);
  if (!parsed.ok()) return Fail(parsed.error());
  const argparse::Namespace& args = parsed.value();
  if (args.Get("verbose") == "True") {
    util::SetupToolLogger(util::LogLevel::kDebug);
  }

  // The valid and invalid --meta values by key; a repeated key keeps its
  // first position and its last value, as in a Python dict.
  std::vector<std::pair<std::string, std::string>> metadata;
  std::vector<std::pair<std::string, std::string>> invalid;
  if (args.Has("meta")) {
    for (const std::string& value : args.GetList("meta")) {
      const size_t equals = value.find('=');
      if (equals == std::string::npos) {
        return Fail(Error{.message = "ValueError: not enough values to "
                                     "unpack (expected 2, got 1)"});
      }
      const std::string key = value.substr(0, equals);
      auto& target =
          std::ranges::find(kMetadataKeys, key) != std::end(kMetadataKeys)
              ? metadata
              : invalid;
      const auto existing = std::ranges::find(
          target, key, &std::pair<std::string, std::string>::first);
      if (existing != target.end()) {
        existing->second = value.substr(equals + 1);
      } else {
        target.emplace_back(key, value.substr(equals + 1));
      }
    }
  }
  if (!invalid.empty()) {
    std::vector<std::string> keys;
    for (const auto& [key, value] : invalid) keys.push_back(key);
    kLog.Error("The following metadata keys are invalid: " +
               util::Join(keys, ", ") +
               ". Valid key values are: analyzer_command, analyzer_version.");
    return 1;
  }

  const Result<void> result = Transform(args, metadata);
  return result.ok() ? 0 : Fail(result.error());
}

}  // namespace
}  // namespace cc::tools

int main(int argc, char* argv[]) {
  cc::platform::InstallTerminateHandler();
  cc::platform::ExitOnTerminationSignals();
  const int exit_code =
      cc::tools::Run(std::vector<std::string>(argv + 1, argv + argc));
  cc::util::Flush(cc::util::Stream::kStdout);
  return exit_code;
}
