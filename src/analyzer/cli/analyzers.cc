#include "analyzer/cli/analyzers.h"

#include <yyjson.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/analyzer_types.h"
#include "common/cli/verbose.h"
#include "platform/subprocess.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/result.h"
#include "util/semver.h"
#include "util/twodim.h"

namespace cc::analyzer::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");

constexpr std::string_view kDescription =
    "Get the list of available and supported analyzers, querying their "
    "version and actual binary executed.";

// The last line of the traceback that the reference prints where it uses
// the binary of an analyzer that was not found (pathlib.Path(None)).
constexpr std::string_view kNoBinaryError =
    "TypeError: argument should be a str or an os.PathLike object where "
    "__fspath__ returns a str, not 'NoneType'";

// The last line of the traceback of --dump-config for an analyzer that has
// no configuration to dump.
constexpr std::string_view kNotImplementedError =
    "NotImplementedError: --dump-config";

// One row of the analyzer list.
struct AnalyzerRow {
  std::string name;
  std::optional<std::string> binary;  // nullopt: not found.
  std::string version;
};

// Runs `command` with stdout passed through, and stderr too unless
// `hide_stderr` (Python: subprocess.call()). Returns the exit code, or the
// error of a command that cannot start.
Result<int> CallPassingOutput(const std::vector<std::string>& command,
                              bool hide_stderr) {
  // The child writes to the same stdout as what is buffered here.
  util::Flush(util::Stream::kStdout);
  const platform::RunResult result = platform::RunProcess(
      command,
      {.env = EnvForBinary(command[0]),
       .stdout_mode = platform::Output::kInherit,
       .stderr_mode = hide_stderr ? platform::Output::kDevNull
                                  : platform::Output::kInherit});
  if (result.start_error != 0) {
    return Result<int>(
        util::OsError(result.start_error, result.start_error_path));
  }
  return Result<int>(result.return_code);
}

// Dumps the configuration of `analyzer`, whose binary is `binary`, to
// stdout (Python: the --dump-config part of main()). Only Clang-Tidy has a
// configuration file; the others print what the reference prints.
Result<void> DumpConfig(std::string_view analyzer,
                        const std::optional<std::string>& binary) {
  if (!binary) {
    util::Print(kNoBinaryError);
    return Result<void>(Error{});
  }
  if (analyzer == "clang-tidy") {
    const Result<int> called =
        CallPassingOutput({*binary, "-dump-config", "-checks=*"},
                          /*hide_stderr=*/false);
    if (!called.ok()) return Result<void>(called.error());
  } else if (analyzer == "clangsa") {
    const Result<int> called = CallPassingOutput(
        {*binary, "-cc1", "-analyzer-checker-option-help",
         "-analyzer-checker-option-help-alpha"},
        /*hide_stderr=*/true);
    if (!called.ok()) return Result<void>(called.error());
    // The flags exist since Clang 9. Without an input file, Clang exits
    // with an error even after printing the help.
    if (called.value() != 0) {
      kLog.Warning("'--dump-config clangsa' is not supported yet. Please "
                   "make sure that you are using Clang 9 or newer.");
    }
  } else if (analyzer == "cppcheck") {
    kLog.Warning("'--dump-config cppcheck' is not supported.");
  } else {
    util::Print(kNotImplementedError);
    return Result<void>(Error{});
  }
  return Result<void>();
}

// Prints the --analyzer-config options of `analyzer` in `format`, and why
// the analyzer cannot run, if it cannot.
void PrintAnalyzerConfig(std::string_view analyzer, std::string_view format,
                         const AnalyzerAvailability& availability) {
  const std::vector<AnalyzerOption> options =
      FindAnalyzer(analyzer)->AnalyzerConfigOptions();
  if (options.empty()) {
    kLog.Warning("No analyzer configurations found for '" +
                 std::string(analyzer) +
                 "'. If you suspsect this shouldn't be the case, try to "
                 "update your analyzer or check whether CodeChecker found "
                 "the intended binary.");
  }
  std::vector<std::vector<std::string>> rows;
  for (const AnalyzerOption& option : options) {
    rows.push_back(
        {std::string(analyzer) + ":" + option.option, option.documentation});
  }
  util::Print(util::twodim::ToString(
      format, util::twodim::KeysForFormat(format, {"Option", "Description"}),
      rows));
  for (const auto& [name, reason] : availability.failed) {
    if (name == analyzer) {
      kLog.Warning("Can't analyze with '" + name + "': " + reason);
    }
  }
}

// Returns the analyzer list in `format`. The binary of an analyzer that was
// not found is an empty cell, and null in JSON (Python: None).
std::string FormatAnalyzerList(std::string_view format,
                               const std::vector<AnalyzerRow>& analyzers) {
  const std::vector<std::string> keys =
      util::twodim::KeysForFormat(format, {"Name", "Path", "Version"});
  if (format == "json") {
    const util::MutableJsonDocument document = util::NewJsonDocument();
    yyjson_mut_doc* doc = document.get();
    yyjson_mut_val* list = yyjson_mut_arr(doc);
    for (const AnalyzerRow& analyzer : analyzers) {
      yyjson_mut_val* object = yyjson_mut_arr_add_obj(doc, list);
      yyjson_mut_obj_add(object, util::MakeJsonString(doc, keys[0]),
                         util::MakeJsonString(doc, analyzer.name));
      yyjson_mut_obj_add(object, util::MakeJsonString(doc, keys[1]),
                         util::MakeJsonOptionalString(doc, analyzer.binary));
      yyjson_mut_obj_add(object, util::MakeJsonString(doc, keys[2]),
                         util::MakeJsonString(doc, analyzer.version));
    }
    return util::DumpJson(list);
  }
  std::vector<std::vector<std::string>> rows;
  for (const AnalyzerRow& analyzer : analyzers) {
    rows.push_back(
        {analyzer.name, analyzer.binary.value_or(""), analyzer.version});
  }
  return util::twodim::ToString(format, keys, rows);
}

}  // namespace

void AddAnalyzersArguments(argparse::Parser& parser) {
  const std::vector<std::string> analyzer_names = SupportedAnalyzerNames();
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));
  parser.AddArgument({.flags = {"--all"},
                      .dest = "all",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .help = "DEPRECATED."});
  parser.AddArgument({.flags = {"--details"},
                      .dest = "details",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .help = "DEPRECATED."});
  parser.AddArgument(
      {.flags = {"--dump-config"},
       .dest = "dump_config",
       .choices = analyzer_names,
       .default_none = true,
       .help = "Dump the available checker options for the given analyzer "
               "to the standard output. Currently only clang-tidy supports "
               "this option. The output can be redirected to a file named "
               ".clang-tidy. If this file is placed to the project directory "
               "then the options are applied to the files under that "
               "directory. This config file can also be provided via "
               "'CodeChecker analyze' and 'CodeChecker check' commands."});
  parser.AddArgument({.flags = {"--analyzer-config"},
                      .dest = "analyzer_config",
                      .choices = analyzer_names,
                      .help = "Show analyzer configuration options. These "
                              "can be given to 'CodeChecker analyze "
                              "--analyzer-config'."});
  parser.AddArgument({.flags = {"-o", "--output"},
                      .dest = "output_format",
                      .choices = {"rows", "table", "csv", "json"},
                      .default_value = "rows",
                      .help = "Specify the format of the output list."});
  common::cli::AddVerboseArgument(parser);
}

int RunAnalyzers(const argparse::Namespace& args) {
  const std::string& format = args.Get("output_format");
  // Only the table format logs to stdout, so the others can be parsed.
  util::SetupLogger(common::cli::Verbose(args), format == "table"
                                                    ? util::Stream::kStdout
                                                    : util::Stream::kStderr);
  const Result<AnalyzerContext*> context = GetContext();
  if (!context.ok()) return context.error().exit_code;
  // The check may replace binaries that are not found by ones in PATH.
  const AnalyzerAvailability availability =
      CheckSupportedAnalyzers(SupportedAnalyzerNames());

  if (args.Has("dump_config")) {
    const std::string& analyzer = args.Get("dump_config");
    const Result<void> dumped =
        DumpConfig(analyzer, context.value()->AnalyzerBinary(analyzer));
    if (!dumped.ok()) {
      if (!dumped.error().message.empty()) {
        util::Print(dumped.error().message);
      }
      return dumped.error().exit_code;
    }
    return 0;
  }

  if (args.Has("analyzer_config")) {
    PrintAnalyzerConfig(args.Get("analyzer_config"), format, availability);
    return 0;
  }

  std::vector<AnalyzerRow> analyzers;
  for (const AnalyzerDriver* analyzer : SupportedAnalyzers()) {
    const std::optional<util::SemVer> version = analyzer->Version();
    analyzers.push_back(
        {.name = std::string(analyzer->name()),
         .binary = context.value()->AnalyzerBinary(analyzer->name()),
         .version = version ? version->ToString() : "NOT FOUND"});
  }
  util::Print(FormatAnalyzerList(format, analyzers));
  for (const auto& [name, reason] : availability.failed) {
    kLog.Warning("Can't analyze with '" + name + "': " + reason);
  }
  return 0;
}

}  // namespace cc::analyzer::cli
