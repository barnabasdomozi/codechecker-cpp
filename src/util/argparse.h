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
// Supported so far: options and positional arguments with nargs None, '*'
// and '+', store_true, store_const, type conversions, required options,
// metavars, custom actions, -h/--help, choices, defaults (including
// argparse.SUPPRESS and None), set_defaults(), argument groups, mutually
// exclusive groups, one level of subcommands, allow_abbrev, the error exit
// code, and the HelpFormatter, RawDescriptionHelpFormatter and
// ArgumentDefaultsHelpFormatter layouts, and CodeChecker's
// RawDescriptionDefaultHelpFormatter.

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
  kRawDescriptionArgumentDefaults,  // Both of the above.
  // CodeChecker's RawDescriptionDefaultHelpFormatter: both of the above,
  // and an option help that starts with "R|" keeps its line breaks.
  kCodeChecker,
};

// How many command line arguments an argument takes (Python: nargs).
enum class Nargs {
  kOne,         // nargs=None: one value.
  kZeroOrMore,  // nargs='*': a list of values.
  kOneOrMore,   // nargs='+': a list of at least one value.
};

// What an argument does when it is given (Python: action).
enum class ArgumentAction {
  kStore,       // Stores its value or values.
  kStoreTrue,   // Takes no value and stores "True".
  kStoreConst,  // Takes no value and stores Argument::const_value.
};

// The values of an argument given by a custom action, or the error that
// ends the parse with the error's exit code (Python: an argparse.Action
// subclass, which can also print and exit). `previous` holds the values the
// destination has so far; null if it has none.
using CustomAction = std::function<Result<std::vector<std::string>>(
    const std::vector<std::string>& values,
    const std::vector<std::string>* previous)>;

// Converts one value of an argument (Python: the `type` of an argument).
// The error message is the text after "argument <name>: ", for example
// "invalid int value: 'x'".
using ArgumentType =
    std::function<Result<std::string>(const std::string& value)>;

// Converts like type=int; the result is the number in decimal.
Result<std::string> IntType(const std::string& value);

// Converts like type=float; the result is the value as given.
Result<std::string> FloatType(const std::string& value);

// An argument, like add_argument(*flags, dest=dest, nargs=nargs,
// action=action, metavar=metavar, choices=choices, default=default_value,
// required=required, help=help). A positional argument has no flags; its
// name is `dest`.
struct Argument {
  std::vector<std::string> flags = {};  // For example "-o", "--output".
  std::string dest = {};  // The name of the value in the Namespace.
  Nargs nargs = Nargs::kOne;
  ArgumentAction action = ArgumentAction::kStore;
  // kStoreConst only: the value to store (Python: const).
  std::string const_value = {};
  // Converts each value before it is checked and stored (Python: type).
  ArgumentType type = {};
  // The name of the value in usage and help; empty: from the choices or
  // `dest`.
  std::string metavar = {};
  std::vector<std::string> choices = {};  // The allowed values; empty: any.
  // The value if the argument is not given. nullopt is argparse.SUPPRESS:
  // then the Namespace has no value for `dest`.
  std::optional<std::string> default_value = {};
  // A list as the default, for example [] (instead of `default_value`).
  std::optional<std::vector<std::string>> default_list = {};
  // The default is Python's None: the Namespace has no value for `dest`,
  // but the help says "(default: None)".
  bool default_none = false;
  bool required = false;
  std::string help = {};
  // Runs instead of storing the values, and returns what to store.
  CustomAction custom_action = {};
  // The help section that lists the argument: an id from
  // Parser::AddArgumentGroup(); 0: "positional arguments" or "options".
  int group = 0;
  // An id from Parser::AddMutuallyExclusiveGroup(); 0: none.
  int exclusive_group = 0;
};

// The values of a parsed command line by their destination names (Python:
// argparse.Namespace). Arguments with nargs '*' or '+' have a list of
// values, the others one value.
class Namespace {
 public:
  Namespace() = default;
  Namespace(const Namespace& other) = default;
  Namespace& operator=(const Namespace& other) = default;

  // Returns whether `dest` has a value (Python: `dest in args`).
  bool Has(std::string_view dest) const;
  // Returns the value of `dest`, which must have one value.
  const std::string& Get(std::string_view dest) const;
  // Returns the list of values of `dest`, which must be present.
  const std::vector<std::string>& GetList(std::string_view dest) const;
  void Set(std::string_view dest, std::string value);
  void SetList(std::string_view dest, std::vector<std::string> values);
  // Sets every value of `other` here.
  void Update(const Namespace& other);

 private:
  std::map<std::string, std::vector<std::string>, std::less<>> values_;
};

namespace internal_argparse {

// One argument of a parser (Python: argparse.Action).
struct Action {
  enum class Kind { kStore, kStoreTrue, kStoreConst, kHelp, kSubparsers };

  Kind kind = Kind::kStore;
  std::vector<std::string> option_strings = {};  // Empty for positionals.
  std::string dest = {};  // Empty means argparse.SUPPRESS.
  Nargs nargs = Nargs::kOne;  // kStore only.
  std::string metavar = {};
  std::vector<std::string> choices = {};
  std::optional<std::string> default_value = {};
  std::optional<std::vector<std::string>> default_list = {};
  bool default_none = false;
  bool required = false;
  std::string help = {};
  std::string const_value = {};  // kStoreConst only.
  ArgumentType type = {};
  CustomAction custom_action = {};
  int group = 0;
  int exclusive_group = 0;
  // kSubparsers only: one help entry per subcommand, listed under it.
  std::vector<Action> subactions = {};
};

// A help section of user-defined arguments (Python: an argument group).
struct Group {
  std::string title;
  std::string description;  // Empty: none.
};

}  // namespace internal_argparse

// A command line parser (Python: argparse.ArgumentParser). It always has
// -h/--help. Parse errors exit with code 1, as in CodeChecker, unless
// set_error_exit_code() says otherwise.
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
  // Sets the exit code of parse errors; plain argparse uses 2. Subcommand
  // parsers added later get the same code.
  void set_error_exit_code(int exit_code) { error_exit_code_ = exit_code; }

  void AddArgument(Argument argument);

  // Adds a help section with `title` and `description` (empty: none), and
  // returns its id for Argument::group (Python: add_argument_group()).
  int AddArgumentGroup(std::string title, std::string description);

  // Adds a group of options of which at most one may be given, and returns
  // its id for Argument::exclusive_group (Python:
  // add_mutually_exclusive_group()). The options must be added one after
  // the other.
  int AddMutuallyExclusiveGroup();

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
  // stderr and returns an error with the error exit code. Errors have no
  // message: they are already printed. A custom action's error is returned
  // as it is.
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
  // Returns how many of the arguments at the start of `pattern` `action`
  // takes, or the error that ends the parse (Python: _match_argument()).
  Result<size_t> MatchArgument(const Action& action,
                               std::string_view pattern) const;
  // Runs `action` with its argument strings; `known` receives the values.
  // Returns the error that ends the parse, if any (Python: take_action()).
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
  int error_exit_code_ = 1;
  std::vector<Action> actions_;
  std::vector<internal_argparse::Group> groups_;
  int exclusive_groups_ = 0;
  std::vector<std::pair<std::string, std::string>> defaults_;
  // The index of the subcommand argument in `actions_`, if there is one.
  std::optional<size_t> subparsers_action_;
  // The subcommand parsers, in the order of the subcommand action's choices.
  std::vector<std::unique_ptr<Parser>> subparsers_;
};

}  // namespace cc::util::argparse

#endif  // CC_UTIL_ARGPARSE_H_
