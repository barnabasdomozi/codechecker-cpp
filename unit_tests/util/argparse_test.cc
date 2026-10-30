#include "util/argparse.h"

#include <stdlib.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// A parser with the argument kinds of report-converter.
void SetUpListParser(Parser& parser) {
  parser.set_help_format(HelpFormat::kArgumentDefaults);
  parser.set_error_exit_code(2);
  parser.AddArgument(
      {.dest = "input", .nargs = Nargs::kOneOrMore, .help = "Inputs."});
  parser.AddArgument({.flags = {"-o"},
                      .dest = "out",
                      .default_none = true,
                      .required = true,
                      .help = "Output."});
  parser.AddArgument({.flags = {"--meta"},
                      .dest = "meta",
                      .nargs = Nargs::kZeroOrMore,
                      .metavar = "META",
                      .default_none = true,
                      .help = "Meta."});
  parser.AddArgument({.flags = {"-c"},
                      .dest = "clean",
                      .action = ArgumentAction::kStoreTrue,
                      .default_value = "False",
                      .help = "Clean."});
}

TEST_F(ArgparseTest, ListArgumentsHelp) {
  Parser parser("prog");
  SetUpListParser(parser);
  EXPECT_EQ(parser.FormatHelp(),
            "usage: prog [-h] -o OUT [--meta [META ...]] [-c] input "
            "[input ...]\n"
            "\n"
            "positional arguments:\n"
            "  input              Inputs.\n"
            "\n"
            "options:\n"
            "  -h, --help         show this help message and exit\n"
            "  -o OUT             Output. (default: None)\n"
            "  --meta [META ...]  Meta. (default: None)\n"
            "  -c                 Clean. (default: False)\n");
}

TEST_F(ArgparseTest, ParsesListArguments) {
  Parser parser("prog");
  SetUpListParser(parser);
  const Result<Namespace> all =
      parser.ParseArgs({"a", "b", "-o", "x", "--meta", "k=v", "-c"});
  ASSERT_TRUE(all.ok());
  EXPECT_EQ(all.value().GetList("input"),
            (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(all.value().Get("out"), "x");
  EXPECT_EQ(all.value().GetList("meta"), std::vector<std::string>{"k=v"});
  EXPECT_EQ(all.value().Get("clean"), "True");

  const Result<Namespace> empty_meta =
      parser.ParseArgs({"-o", "x", "a", "--meta"});
  ASSERT_TRUE(empty_meta.ok());
  EXPECT_TRUE(empty_meta.value().GetList("meta").empty());
  EXPECT_EQ(empty_meta.value().Get("clean"), "False");
}

TEST_F(ArgparseTest, ListArgumentErrors) {
  constexpr std::string_view kUsage =
      "usage: prog [-h] -o OUT [--meta [META ...]] [-c] input [input ...]\n";
  const std::vector<std::pair<std::vector<std::string>, std::string>> cases =
      {
          // --meta takes the input too.
          {{"-o", "x", "--meta", "k"},
           "prog: error: the following arguments are required: input\n"},
          {{"a"}, "prog: error: the following arguments are required: -o\n"},
          {{"-c=1", "a", "-o", "x"},
           "prog: error: argument -c: ignored explicit argument '1'\n"},
          {{"a", "-o"}, "prog: error: argument -o: expected one argument\n"},
      };
  for (const auto& [args, message] : cases) {
    Parser parser("prog");
    SetUpListParser(parser);
    testing::internal::CaptureStderr();
    const Result<Namespace> result = parser.ParseArgs(args);
    const std::string output = testing::internal::GetCapturedStderr();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().exit_code, 2);
    EXPECT_EQ(output, std::string(kUsage) + message);
  }
}

// A parser with nargs '?' arguments, like the label filters of `checkers`.
void SetUpOptionalValueParser(Parser& parser) {
  parser.set_help_format(HelpFormat::kArgumentDefaults);
  parser.AddArgument({.flags = {"--label"},
                      .dest = "label",
                      .nargs = Nargs::kZeroOrOne,
                      .help = "Label."});
  parser.AddArgument({.flags = {"--level"},
                      .dest = "level",
                      .nargs = Nargs::kZeroOrOne,
                      .choices = {"a", "b"},
                      .default_value = "a",
                      .help = "Level."});
  parser.AddArgument({.dest = "name",
                      .nargs = Nargs::kZeroOrOne,
                      .default_value = "x",
                      .help = "Name."});
}

TEST_F(ArgparseTest, OptionalValueHelp) {
  Parser parser("prog");
  SetUpOptionalValueParser(parser);
  EXPECT_EQ(parser.FormatHelp(),
            "usage: prog [-h] [--label [LABEL]] [--level [{a,b}]] [name]\n"
            "\n"
            "positional arguments:\n"
            "  name             Name. (default: x)\n"
            "\n"
            "options:\n"
            "  -h, --help       show this help message and exit\n"
            "  --label [LABEL]  Label.\n"
            "  --level [{a,b}]  Level. (default: a)\n");
}

TEST_F(ArgparseTest, ParsesOptionalValues) {
  Parser parser("prog");
  SetUpOptionalValueParser(parser);

  const Result<Namespace> absent = parser.ParseArgs({});
  ASSERT_TRUE(absent.ok());
  EXPECT_FALSE(absent.value().Has("label"));
  EXPECT_EQ(absent.value().Get("level"), "a");
  EXPECT_EQ(absent.value().Get("name"), "x");

  // Without a value, an option stores its const: Python's None, "" here.
  const Result<Namespace> no_value = parser.ParseArgs({"--label"});
  ASSERT_TRUE(no_value.ok());
  EXPECT_EQ(no_value.value().Get("label"), "");

  const Result<Namespace> value = parser.ParseArgs({"--label", "k:v"});
  ASSERT_TRUE(value.ok());
  EXPECT_EQ(value.value().Get("label"), "k:v");
  EXPECT_EQ(value.value().Get("name"), "x");

  const Result<Namespace> explicit_empty = parser.ParseArgs({"--label=", "n"});
  ASSERT_TRUE(explicit_empty.ok());
  EXPECT_EQ(explicit_empty.value().Get("label"), "");
  EXPECT_EQ(explicit_empty.value().Get("name"), "n");

  // An option after the option ends it, and its const skips the choices.
  const Result<Namespace> next_option =
      parser.ParseArgs({"--level", "--label", "k"});
  ASSERT_TRUE(next_option.ok());
  EXPECT_EQ(next_option.value().Get("level"), "");
  EXPECT_EQ(next_option.value().Get("label"), "k");
}

TEST_F(ArgparseTest, OptionalValueInvalidChoice) {
  Parser parser("prog");
  SetUpOptionalValueParser(parser);
  testing::internal::CaptureStderr();
  const Result<Namespace> result = parser.ParseArgs({"--level", "c"});
  const std::string output = testing::internal::GetCapturedStderr();
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().exit_code, 1);
  EXPECT_EQ(output,
            "usage: prog [-h] [--label [LABEL]] [--level [{a,b}]] [name]\n"
            "prog: error: argument --level: invalid choice: 'c' (choose from "
            "a, b)\n");
}

// A parser with a required, an optional and a required one-option mutually
// exclusive group.
void SetUpExclusiveParser(Parser& parser) {
  const int input =
      parser.AddMutuallyExclusiveGroup(ExclusiveGroup::kRequired);
  parser.AddArgument(
      {.flags = {"-b"}, .dest = "command", .exclusive_group = input});
  parser.AddArgument(
      {.flags = {"-l"}, .dest = "logfile", .exclusive_group = input});
  const int mode = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument({.flags = {"--x"},
                      .dest = "x",
                      .action = ArgumentAction::kStoreTrue,
                      .exclusive_group = mode});
  parser.AddArgument({.flags = {"--y"},
                      .dest = "y",
                      .action = ArgumentAction::kStoreTrue,
                      .exclusive_group = mode});
  const int single =
      parser.AddMutuallyExclusiveGroup(ExclusiveGroup::kRequired);
  parser.AddArgument({.flags = {"--z"},
                      .dest = "z",
                      .action = ArgumentAction::kStoreTrue,
                      .exclusive_group = single});
}

TEST_F(ArgparseTest, ExclusiveGroupsUsage) {
  Parser parser("prog");
  SetUpExclusiveParser(parser);
  EXPECT_EQ(parser.FormatUsage(),
            "usage: prog [-h] (-b COMMAND | -l LOGFILE) [--x | --y] --z\n");
}

TEST_F(ArgparseTest, ExclusiveGroupErrors) {
  constexpr std::string_view kUsage =
      "usage: prog [-h] (-b COMMAND | -l LOGFILE) [--x | --y] --z\n";
  const std::vector<std::pair<std::vector<std::string>, std::string>> cases =
      {
          {{}, "prog: error: one of the arguments -b -l is required\n"},
          {{"-b", "make"},
           "prog: error: one of the arguments --z is required\n"},
          {{"-b", "m", "-l", "f", "--z"},
           "prog: error: argument -l: not allowed with argument -b\n"},
      };
  for (const auto& [args, message] : cases) {
    Parser parser("prog");
    SetUpExclusiveParser(parser);
    testing::internal::CaptureStderr();
    const Result<Namespace> result = parser.ParseArgs(args);
    const std::string output = testing::internal::GetCapturedStderr();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().exit_code, 1);
    EXPECT_EQ(output, std::string(kUsage) + message);
  }
  Parser parser("prog");
  SetUpExclusiveParser(parser);
  const Result<Namespace> result = parser.ParseArgs({"-b", "make", "--z"});
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().Get("command"), "make");
  EXPECT_FALSE(result.value().Has("logfile"));
}

TEST_F(ArgparseTest, CustomActionReplacesTheValues) {
  Parser parser("prog");
  parser.AddArgument(
      {.dest = "input",
       .nargs = Nargs::kOneOrMore,
       .custom_action = [](const std::vector<std::string>& values,
                           const std::vector<std::string>* /*previous*/) {
         return Result<std::vector<std::string>>(
             std::vector<std::string>{values.back()});
       }});
  const Result<Namespace> result = parser.ParseArgs({"a", "b"});
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().GetList("input"), std::vector<std::string>{"b"});
}

}  // namespace
}  // namespace cc::util::argparse
