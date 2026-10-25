// The codechecker-cpp program: the entry point that parses the command line
// and runs a subcommand (architecture.md §5.2). Port of
// codechecker_common/cli.py.

#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/package.h"
#include "launcher/subcommands.h"
#include "platform/process.h"
#include "util/argparse.h"
#include "util/result.h"

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

int Main(const std::vector<std::string>& arguments) {
  std::vector<std::string> args = arguments;
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
  parser.AddSubparsers("commands");
  for (const Subcommand& subcommand : subcommands) {
    util::argparse::Parser& subparser = parser.AddParser(
        std::string(subcommand.name), std::string(subcommand.help));
    subparser.SetDefault(std::string(kSubcommandDest),
                         std::string(subcommand.name));
    subcommand.add_arguments(subparser);
  }

  const Result<util::argparse::Namespace> parsed = parser.ParseArgs(args);
  if (!parsed.ok()) return parsed.error().exit_code;
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

int main(int argc, char* argv[]) {
  cc::platform::InstallTerminateHandler();
  cc::platform::ExitOnTerminationSignals();
  return cc::launcher::Main(std::vector<std::string>(argv + 1, argv + argc));
}
