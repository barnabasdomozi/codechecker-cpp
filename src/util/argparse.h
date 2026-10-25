// A clone of the parts of Python's argparse (3.13) that the commands use
// (libraries.md, "argparse clone"): the same parsing rules, help and usage
// layout, and error messages.
//
//   argparse::Parser parser("codechecker-cpp version");
//   parser.AddArgument({.flags = {"-o", "--output"},
//                       .dest = "output_format",
//                       .choices = {"rows", "table", "csv", "json"},
//                       .default_value = "table",
//                       .help = "The format to use."});
//   Result<argparse::Namespace> args = parser.ParseArgs(arguments);
//   if (!args.ok()) return args.error().exit_code;
//   Print(args.value().Get("output_format"));
//
// Supported so far: options that take one value, -h/--help, choices,
// defaults (including argparse.SUPPRESS), set_defaults(), one level of
// subcommands, allow_abbrev, and the HelpFormatter,
// RawDescriptionHelpFormatter and ArgumentDefaultsHelpFormatter layouts.

#ifndef CC_UTIL_ARGPARSE_H_
#define CC_UTIL_ARGPARSE_H_

#include <stddef.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/result.h"

namespace cc::util::argparse {

// The help layout of a parser (Python: formatter_class).
enum class HelpFormat {
  kDefault,           // HelpFormatter.
  kRawDescription,    // RawDescriptionHelpFormatter: the description and
                      // the epilog are printed as they are written.
  kArgumentDefaults,  // ArgumentDefaultsHelpFormatter: option help ends with
                      // "(default: <value>)".
};

// An option that takes one value, like add_argument(*flags, dest=dest,
// choices=choices, default=default_value, help=help).
struct Argument {
  std::vector<std::string> flags;  // For example "-o", "--output".
  std::string dest;                // The name of the value in the Namespace.
  std::vector<std::string> choices;  // The allowed values; empty: any value.
  // The value if the option is not given. nullopt is argparse.SUPPRESS: then
  // the Namespace has no value for `dest`.
  std::optional<std::string> default_value;
  std::string help;
};

// The values of a parsed command line by their destination names (Python:
// argparse.Namespace).
class Namespace {
 public:
  Namespace() = default;
  Namespace(const Namespace& other) = default;
  Namespace& operator=(const Namespace& other) = default;

  // Returns whether `dest` has a value (Python: `dest in args`).
  bool Has(std::string_view dest) const;
  // Returns the value of `dest`, which must be present.
  const std::string& Get(std::string_view dest) const;
  void Set(std::string_view dest, std::string value);
  // Sets every value of `other` here.
  void Update(const Namespace& other);

 private:
  std::map<std::string, std::string, std::less<>> values_;
};

namespace internal_argparse {

// One argument of a parser (Python: argparse.Action).
struct Action {
  enum class Kind { kStore, kHelp, kSubparsers };

  Kind kind = Kind::kStore;
  std::vector<std::string> option_strings = {};  // Empty for positionals.
  std::string dest = {};  // Empty means argparse.SUPPRESS.
  std::vector<std::string> choices = {};
  std::optional<std::string> default_value = {};
  std::string help = {};
  // kSubparsers only: one help entry per subcommand, listed under it.
  std::vector<Action> subactions = {};
};

}  // namespace internal_argparse

// A command line parser (Python: argparse.ArgumentParser). It always has
// -h/--help. Parse errors exit with code 1, as in CodeChecker, not with
// argparse's 2.
class Parser {
 public:
  explicit Parser(std::string prog);

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  const std::string& prog() const { return prog_; }
  void set_prog(std::string prog) { prog_ = std::move(prog); }
  void set_description(std::string description) {
    description_ = std::move(description);
  }
  void set_epilog(std::string epilog) { epilog_ = std::move(epilog); }
  void set_help_format(HelpFormat help_format) { help_format_ = help_format; }
  void set_allow_abbrev(bool allow_abbrev) { allow_abbrev_ = allow_abbrev; }

  void AddArgument(Argument argument);

  // Sets `dest` to `value` in the result of every parse with this parser
  // (Python: set_defaults(dest=value)).
  void SetDefault(std::string dest, std::string value);

  // Adds the subcommand argument, listed with `help` (Python:
  // add_subparsers(help=help)). Call it once, before AddParser().
  void AddSubparsers(std::string help);

  // Adds the subcommand `name`, listed with `help`, and returns its parser.
  // Its prog is "<prog> <name>" (Python: add_parser(name, help=help)).
  Parser& AddParser(std::string name, std::string help);

  // Parses `args`, the command line without the program name (Python:
  // parse_args()). For -h, prints the help on stdout and returns an error
  // with exit code 0. For a parse error, prints the usage and the error on
  // stderr and returns an error with exit code 1. Errors have no message:
  // they are already printed.
  Result<Namespace> ParseArgs(const std::vector<std::string>& args) const;

  std::string FormatUsage() const;
  std::string FormatHelp() const;

 private:
  using Action = internal_argparse::Action;
  struct OptionTuple;
  struct KnownArgs;

  // Python: parse_known_args().
  Result<KnownArgs> ParseKnownArgs(const std::vector<std::string>& args) const;
  // Returns how `arg` is used: nullopt if it is a positional argument, else
  // the options it may stand for (Python: _parse_optional()).
  std::optional<std::vector<OptionTuple>> ParseOptional(
      const std::string& arg) const;
  std::vector<OptionTuple> OptionTuples(const std::string& arg) const;
  const Action* FindOption(std::string_view option_string) const;
  // Runs `action` with its argument strings; `known` receives the values.
  // Returns the error that ends the parse, if any.
  std::optional<Error> TakeAction(const Action& action,
                                  const std::vector<std::string>& values,
                                  KnownArgs* known) const;
  // Prints the usage and "<prog>: error: <message>" on stderr and returns
  // the error to end the command with (Python: error()).
  Error Fail(std::string_view message) const;

  std::string prog_;
  std::string description_;
  std::string epilog_;
  HelpFormat help_format_ = HelpFormat::kDefault;
  bool allow_abbrev_ = true;
  std::vector<Action> actions_;
  std::vector<std::pair<std::string, std::string>> defaults_;
  // The index of the subcommand argument in `actions_`, if there is one.
  std::optional<size_t> subparsers_action_;
  // The subcommand parsers, in the order of the subcommand action's choices.
  std::vector<std::unique_ptr<Parser>> subparsers_;
};

}  // namespace cc::util::argparse

#endif  // CC_UTIL_ARGPARSE_H_
