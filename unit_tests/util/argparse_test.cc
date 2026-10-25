#include "util/argparse.h"

#include <stdlib.h>

#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "util/result.h"

namespace cc::util::argparse {
namespace {

// A parser like the one of `codechecker-cpp version`.
void SetUpVersionParser(Parser& parser) {
  parser.set_description(
      "Print the version of CodeChecker package that is being used.");
  parser.set_help_format(HelpFormat::kArgumentDefaults);
  parser.set_allow_abbrev(false);
  parser.AddArgument({.flags = {"-o", "--output"},
                      .dest = "output_format",
                      .choices = {"rows", "table", "csv", "json"},
                      .default_value = "table",
                      .help = "The format to use when printing the version."});
  parser.AddArgument({.flags = {"--verbose"},
                      .dest = "verbose",
                      .choices = {"info", "debug_analyzer", "debug"},
                      .default_value = std::nullopt,
                      .help = "Set verbosity level."});
}

class ArgparseTest : public testing::Test {
 protected:
  void SetUp() override { setenv("COLUMNS", "80", 1); }
  void TearDown() override { unsetenv("COLUMNS"); }

  // Parses `args` with a top-level parser that has `version` and returns
  // what it printed on stderr. `exit_code` receives the exit code.
  std::string ParseError(const std::vector<std::string>& args,
                         int* exit_code) {
    Parser parser("codechecker-cpp");
    parser.AddSubparsers("commands");
    SetUpVersionParser(parser.AddParser("version", "Print the version."));
    testing::internal::CaptureStderr();
    const Result<Namespace> result = parser.ParseArgs(args);
    std::string output = testing::internal::GetCapturedStderr();
    *exit_code = result.ok() ? 0 : result.error().exit_code;
    return output;
  }
};

TEST_F(ArgparseTest, HelpWithDefaults) {
  Parser parser("codechecker-cpp version");
  SetUpVersionParser(parser);
  EXPECT_EQ(parser.FormatHelp(),
            "usage: codechecker-cpp version [-h] [-o {rows,table,csv,json}]\n"
            "                               [--verbose "
            "{info,debug_analyzer,debug}]\n"
            "\n"
            "Print the version of CodeChecker package that is being used.\n"
            "\n"
            "options:\n"
            "  -h, --help            show this help message and exit\n"
            "  -o, --output {rows,table,csv,json}\n"
            "                        The format to use when printing the "
            "version. (default:\n"
            "                        table)\n"
            "  --verbose {info,debug_analyzer,debug}\n"
            "                        Set verbosity level.\n");
}

TEST_F(ArgparseTest, NarrowHelp) {
  setenv("COLUMNS", "40", 1);
  Parser parser("codechecker-cpp version");
  SetUpVersionParser(parser);
  EXPECT_EQ(parser.FormatHelp(),
            "usage: codechecker-cpp version\n"
            "       [-h] [-o {rows,table,csv,json}]\n"
            "       [--verbose {info,debug_analyzer,debug}]\n"
            "\n"
            "Print the version of CodeChecker\n"
            "package that is being used.\n"
            "\n"
            "options:\n"
            "  -h, --help      show this help\n"
            "                  message and exit\n"
            "  -o, --output {rows,table,csv,json}\n"
            "                  The format to use\n"
            "                  when printing the\n"
            "                  version. (default:\n"
            "                  table)\n"
            "  --verbose {info,debug_analyzer,debug}\n"
            "                  Set verbosity level.\n");
}

TEST_F(ArgparseTest, HelpWithSubcommandsAndRawDescription) {
  Parser parser("codechecker-cpp");
  parser.set_help_format(HelpFormat::kRawDescription);
  parser.set_description("Line one.\nLine two.");
  parser.set_epilog("Epilog\n  indented ");
  parser.AddSubparsers("commands");
  parser.AddParser("version",
                   "Print the version of CodeChecker package that is being "
                   "used.");
  parser.AddParser("analyzer-version",
                   "Print the version of CodeChecker analyzer package that is "
                   "being used.");
  EXPECT_EQ(parser.FormatHelp(),
            "usage: codechecker-cpp [-h] {version,analyzer-version} ...\n"
            "\n"
            "Line one.\n"
            "Line two.\n"
            "\n"
            "positional arguments:\n"
            "  {version,analyzer-version}\n"
            "                        commands\n"
            "    version             Print the version of CodeChecker package "
            "that is being\n"
            "                        used.\n"
            "    analyzer-version    Print the version of CodeChecker analyzer "
            "package that\n"
            "                        is being used.\n"
            "\n"
            "options:\n"
            "  -h, --help            show this help message and exit\n"
            "\n"
            "Epilog\n"
            "  indented \n");
}

TEST_F(ArgparseTest, ParsesValues) {
  Parser parser("codechecker-cpp");
  parser.AddSubparsers("commands");
  Parser& version = parser.AddParser("version", "");
  SetUpVersionParser(version);
  version.SetDefault("func", "version");
  for (const std::vector<std::string>& args :
       {std::vector<std::string>{"version", "-o", "json"},
        {"version", "-ojson"},
        {"version", "--output=json"},
        {"version", "-o=json"}}) {
    const Result<Namespace> result = parser.ParseArgs(args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().Get("output_format"), "json");
    EXPECT_EQ(result.value().Get("func"), "version");
    EXPECT_FALSE(result.value().Has("verbose"));
  }
  const Result<Namespace> defaults = parser.ParseArgs({"version"});
  ASSERT_TRUE(defaults.ok());
  EXPECT_EQ(defaults.value().Get("output_format"), "table");
  EXPECT_FALSE(parser.ParseArgs({}).value().Has("func"));
}

TEST_F(ArgparseTest, InvalidChoice) {
  int exit_code = 0;
  EXPECT_EQ(ParseError({"version", "-o", "foo"}, &exit_code),
            "usage: codechecker-cpp version [-h] [-o {rows,table,csv,json}]\n"
            "                               [--verbose "
            "{info,debug_analyzer,debug}]\n"
            "codechecker-cpp version: error: argument -o/--output: invalid "
            "choice: 'foo' (choose from rows, table, csv, json)\n");
  EXPECT_EQ(exit_code, 1);
}

TEST_F(ArgparseTest, MissingValue) {
  int exit_code = 0;
  const std::string output = ParseError({"version", "--verbose"}, &exit_code);
  EXPECT_TRUE(output.ends_with(
      "codechecker-cpp version: error: argument --verbose: expected one "
      "argument\n"))
      << output;
  EXPECT_EQ(exit_code, 1);
}

TEST_F(ArgparseTest, UnrecognizedArgumentsUseTheTopLevelUsage) {
  int exit_code = 0;
  // Long options cannot be abbreviated, so --out is unknown.
  EXPECT_EQ(ParseError({"--bogus", "version", "--out", "json"}, &exit_code),
            "usage: codechecker-cpp [-h] {version} ...\n"
            "codechecker-cpp: error: unrecognized arguments: --bogus --out "
            "json\n");
  EXPECT_EQ(exit_code, 1);
}

TEST_F(ArgparseTest, UnknownSubcommand) {
  int exit_code = 0;
  EXPECT_EQ(ParseError({"foo"}, &exit_code),
            "usage: codechecker-cpp [-h] {version} ...\n"
            "codechecker-cpp: error: argument {version}: invalid choice: "
            "'foo' (choose from version)\n");
  EXPECT_EQ(exit_code, 1);
}

TEST_F(ArgparseTest, HelpExitsWithZero) {
  Parser parser("prog");
  testing::internal::CaptureStdout();
  const Result<Namespace> result = parser.ParseArgs({"-h"});
  const std::string output = testing::internal::GetCapturedStdout();
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().exit_code, 0);
  EXPECT_EQ(output,
            "usage: prog [-h]\n\noptions:\n"
            "  -h, --help  show this help message and exit\n");
}

}  // namespace
}  // namespace cc::util::argparse
