// The codechecker-cpp program: the entry point that parses the command line
// and runs a subcommand (architecture.md §5.2). Port of
// codechecker_common/cli.py.

#include <stdlib.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/cli/logo.h"
#include "common/cli/verbose.h"
#include "common/cmd_config.h"
#include "common/package.h"
#include "launcher/subcommands.h"
#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/subprocess.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::launcher {
namespace {

constexpr std::string_view kDescription =
    "Run the CodeChecker sourcecode analyzer framework.\n"
    "Please specify a subcommand to access individual features.";

constexpr std::string_view kEpilog =
    "Example scenario: Analyzing, and storing results\n"
    "------------------------------------------------\n"
    "Start the server where the results will be stored and can be viewed\n"
    "after the analysis is done:\n"
    "    codechecker-cpp server\n"
    "\n"
    "Analyze a project with default settings:\n"
    "    codechecker-cpp check -b \"cd ~/myproject && make\" -o \"~/results\"\n"
    "\n"
    "Store the analyzer results to the server:\n"
    "    codechecker-cpp store \"~/results\" -n myproject\n"
    "\n"
    "The results can be viewed:\n"
    " * In a web browser: http://localhost:8001\n"
    " * In the command line:\n"
    "    codechecker-cpp cmd results myproject\n"
    "\n"
    "Example scenario: Analyzing, and printing results to Terminal (no storage)\n"
    "--------------------------------------------------------------------------\n"
    "In this case, no database is used, and the results are printed on the standard\n"
    "output.\n"
    "\n"
    "    codechecker-cpp check -b \"cd ~/myproject && make\" ";

// The Namespace entry that names the subcommand to run (Python: the `func`
// default of each subcommand parser).
constexpr std::string_view kSubcommandDest = "func";

// Replaces --config FILE in `argv` with the options of the file and parses
// the command line again (Python: the func_process_config_file part of
// cli.main()). Returns the parse result.
Result<util::argparse::Namespace> SpliceConfigFile(
    util::argparse::Parser& parser, const util::argparse::Namespace& values,
    std::vector<std::string>* argv) {
  util::SetupLogger(common::cli::Verbose(values), util::Stream::kStderr);
  constexpr util::Logger kLog("system");
  const Result<std::vector<std::string>> options =
      common::ProcessConfigFile(values, (*argv)[1]);
  if (!options.ok()) {
    return Result<util::argparse::Namespace>(options.error());
  }
  if (options.value().empty()) {
    return Result<util::argparse::Namespace>(values);
  }
  std::vector<std::string> expanded;
  for (const std::string& option : options.value()) {
    expanded.push_back(util::pypath::Expandvars(option));
  }
  const auto config = std::ranges::find(*argv, "--config");
  // Python: sys.argv.index() raises an exception for --config=FILE.
  if (config == argv->end() || config + 1 == argv->end()) std::terminate();
  const auto at = argv->erase(config, config + 2);
  argv->insert(at, expanded.begin(), expanded.end());
  Result<util::argparse::Namespace> parsed = parser.ParseArgs(
      std::vector<std::string>(argv->begin() + 1, argv->end()));
  if (parsed.ok()) {
    kLog.Info("Full extended command: " + util::Join(*argv, " "));
  }
  return parsed;
}

// The entry point in the child process (architecture.md §5.2). `argv`
// starts with the program name.
int Main(std::vector<std::string> argv) {
  // The package root is the parent of the bin/ folder.
  if (const std::optional<std::string> bin_dir =
          platform::GetEnv("CC_BIN_DIR")) {
    setenv("CC_DATA_FILES_DIR", util::pypath::Dirname(*bin_dir).c_str(), 1);
  } else {
    setenv("CC_DATA_FILES_DIR", common::DataFilesDir().c_str(), 1);
  }
  if (platform::GetEnv("CC_LIB_DIR").value_or("").empty()) {
    setenv("CC_LIB_DIR",
           util::pypath::Join({common::DataFilesDir(), "lib", "codechecker"})
               .c_str(),
           1);
  }
  platform::ExitOnTerminationSignals();
  platform::IgnoreBrokenPipes();
  common::SetCommandLine(argv);

  std::vector<std::string> args(argv.begin() + 1, argv.end());
  std::vector<Subcommand> subcommands;
  for (const std::span<const Subcommand> part :
       {AnalyzerSubcommands(), WebSubcommands()}) {
    subcommands.insert(subcommands.end(), part.begin(), part.end());
  }
  // If the first argument is a subcommand, only its parser is built.
  for (const Subcommand& subcommand : subcommands) {
    if (!args.empty() && args[0] == subcommand.name) {
      subcommands = {subcommand};
      break;
    }
  }

  util::argparse::Parser parser{std::string(common::kProgramName)};
  parser.set_help_format(util::argparse::HelpFormat::kRawDescription);
  parser.set_description(std::string(kDescription));
  parser.set_epilog(std::string(kEpilog));
  parser.set_help_banner(common::cli::TerminalLogo());
  parser.AddSubparsers("commands");
  for (const Subcommand& subcommand : subcommands) {
    util::argparse::Parser& subparser = parser.AddParser(
        std::string(subcommand.name), std::string(subcommand.help));
    subparser.SetDefault(std::string(kSubcommandDest),
                         std::string(subcommand.name));
    subcommand.add_arguments(subparser);
  }

  Result<util::argparse::Namespace> parsed = parser.ParseArgs(args);
  if (!parsed.ok()) return parsed.error().exit_code;
  if (subcommands.size() == 1 && subcommands[0].config_file &&
      parsed.value().Has(kSubcommandDest)) {
    parsed = SpliceConfigFile(parser, parsed.value(), &argv);
    if (!parsed.ok()) return parsed.error().exit_code;
    common::SetCommandLine(argv);
  }
  const util::argparse::Namespace& values = parsed.value();
  if (!values.Has(kSubcommandDest)) {
    // Without a subcommand, the top-level help is printed.
    args.push_back("--help");
    const Result<util::argparse::Namespace> help = parser.ParseArgs(args);
    return help.ok() ? 0 : help.error().exit_code;
  }
  for (const Subcommand& subcommand : subcommands) {
    if (subcommand.name == values.Get(kSubcommandDest)) {
      return subcommand.run(values);
    }
  }
  // The parser knows no other subcommands.
  std::terminate();
}

}  // namespace
}  // namespace cc::launcher

// The launcher (architecture.md §5.1): saves the environment for the
// commands that run the build, then runs the entry point in a child process
// and removes the saved environment when it ends.
int main(int argc, char* argv[]) {
  cc::platform::InstallTerminateHandler();
  const std::vector<std::string> args(argv, argv + argc);
  const std::optional<std::string> temp_dir = cc::platform::MakeTempDir();
  if (temp_dir) {
    const std::string env_file =
        cc::util::pypath::Join({*temp_dir, "original_env"});
    std::string content;
    for (const std::string& item : cc::platform::CurrentEnvironment()) {
      content += item;
      content.push_back('\0');
    }
    if (cc::util::WriteFile(env_file, content).ok()) {
      setenv("CODECHECKER_ORIGINAL_BUILD_ENV", env_file.c_str(), 1);
    } else {
      cc::util::Print("Saving original build environment failed.");
    }
  }
  setenv("CC_BIN_DIR",
         cc::util::pypath::Dirname(cc::platform::ExecutablePath()).c_str(), 1);
  const int exit_code =
      cc::platform::RunForked([&args] { return cc::launcher::Main(args); });
  if (temp_dir) cc::platform::RemoveTree(*temp_dir);
  return exit_code;
}
