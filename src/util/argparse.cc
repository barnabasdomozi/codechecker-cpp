#include "util/argparse.h"

#include <stddef.h>

#include <algorithm>
#include <exception>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/print.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/terminal.h"
#include "util/textwrap.h"
#include "util/unicode.h"

namespace cc::util::argparse {
namespace {

using internal_argparse::Action;

int Length(std::string_view text) {
  return static_cast<int>(CodePointCount(text));
}

std::string Spaces(int count) { return std::string(std::max(count, 0), ' '); }

std::string Upper(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return result;
}

// Replaces each run of ASCII whitespace with one space and strips the ends
// (HelpFormatter's _whitespace_matcher).
std::string NormalizeWhitespace(std::string_view text) {
  std::string result;
  bool space = false;
  for (const char c : StripAsciiWhitespace(text)) {
    const bool is_space = c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
                          c == '\f' || c == '\r';
    if (is_space && !space) result += ' ';
    if (!is_space) result += c;
    space = is_space;
  }
  return result;
}

// Expands the %(name)s references of a help text, and %% (Python:
// `text % params`). `params` holds the names that have a value.
std::string ExpandPercent(
    std::string_view text,
    const std::vector<std::pair<std::string_view, std::string>>& params) {
  std::string result;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t percent = text.find('%', pos);
    result.append(text.substr(pos, percent - pos));
    if (percent == std::string_view::npos) break;
    pos = percent + 1;
    if (text.substr(pos).starts_with('%')) {
      result += '%';
      ++pos;
      continue;
    }
    const size_t close = text.find(")s", pos);
    if (text.substr(pos).starts_with('(') && close != std::string_view::npos) {
      const std::string_view name = text.substr(pos + 1, close - pos - 1);
      const auto param = std::ranges::find(
          params, name,
          &std::pair<std::string_view, std::string>::first);
      if (param != params.end()) {
        result += param->second;
        pos = close + 2;
        continue;
      }
    }
    result += '%';
  }
  return result;
}

// Returns the metavar argparse shows for `action`: its choices in braces,
// else `default_metavar` (Python: _metavar_formatter()).
std::string Metavar(const Action& action, std::string_view default_metavar) {
  if (!action.choices.empty()) return "{" + Join(action.choices, ",") + "}";
  return std::string(default_metavar);
}

// The name of `action` in error messages (Python: _get_action_name()).
std::string ActionName(const Action& action) {
  if (!action.option_strings.empty()) return Join(action.option_strings, "/");
  if (!action.dest.empty()) return action.dest;
  return Metavar(action, "");
}

std::string ArgumentError(const Action& action, std::string_view message) {
  return std::format("argument {}: {}", ActionName(action), message);
}

// Formats the usage and help of one parser (Python: HelpFormatter and its
// subclasses).
class HelpFormatter {
 public:
  // `actions` must outlive the formatter.
  HelpFormatter(std::string_view prog, HelpFormat format,
                const std::vector<Action>* actions);

  HelpFormatter(const HelpFormatter&) = delete;
  HelpFormatter& operator=(const HelpFormatter&) = delete;

  // Returns "usage: ...\n\n" (Python: _format_usage()).
  std::string Usage() const;
  // Returns a description or an epilog followed by a blank line, or nothing
  // for an empty text (Python: _format_text()).
  std::string Text(std::string_view text) const;
  // Returns a section of argument help: a heading and its arguments, or
  // nothing if there are no arguments.
  std::string Section(std::string_view heading, bool positional) const;

  // Returns the final text from the pieces above (Python: format_help()).
  static std::string Finish(const std::string& help);

 private:
  std::vector<std::string> UsageParts(bool positional) const;
  std::string Invocation(const Action& action) const;
  std::string ExpandHelp(const Action& action) const;
  // Python: _format_action().
  std::string FormatAction(const Action& action, int indent) const;

  std::string_view prog_;
  HelpFormat format_;
  const std::vector<Action>* actions_;
  int width_;
  int max_help_position_;
  // The longest invocation plus its indent (Python: _action_max_length).
  int action_max_length_ = 0;
};

HelpFormatter::HelpFormatter(std::string_view prog, HelpFormat format,
                             const std::vector<Action>* actions)
    : prog_(prog),
      format_(format),
      actions_(actions),
      width_(TerminalColumns() - 2),
      max_help_position_(std::min(24, std::max(width_ - 20, 4))) {
  // Arguments are indented by 2 in their section, subactions by 4.
  for (const Action& action : *actions_) {
    action_max_length_ =
        std::max(action_max_length_, Length(Invocation(action)) + 2);
    for (const Action& subaction : action.subactions) {
      action_max_length_ =
          std::max(action_max_length_, Length(Invocation(subaction)) + 4);
    }
  }
}

std::vector<std::string> HelpFormatter::UsageParts(bool positional) const {
  std::vector<std::string> parts;
  for (const Action& action : *actions_) {
    if (action.option_strings.empty() != positional) continue;
    if (positional) {
      // Only the subcommand argument is positional (nargs=PARSER).
      parts.push_back(Metavar(action, action.dest) + " ...");
    } else if (action.kind == Action::Kind::kHelp) {
      parts.push_back("[" + action.option_strings[0] + "]");
    } else {
      parts.push_back("[" + action.option_strings[0] + " " +
                      Metavar(action, Upper(action.dest)) + "]");
    }
  }
  return parts;
}

std::string HelpFormatter::Usage() const {
  constexpr std::string_view kPrefix = "usage: ";
  const std::vector<std::string> optionals = UsageParts(false);
  const std::vector<std::string> positionals = UsageParts(true);
  std::vector<std::string> all = optionals;
  all.insert(all.end(), positionals.begin(), positionals.end());
  std::string usage(prog_);
  if (!all.empty()) usage += " " + Join(all, " ");
  const int text_width = width_;
  const int prefix_length = Length(kPrefix);
  if (prefix_length + Length(usage) <= text_width) {
    return std::format("{}{}\n\n", kPrefix, usage);
  }
  // Wraps `parts` into lines; with `has_prefix`, the first line follows the
  // prefix instead of the indent.
  const auto get_lines = [&](const std::vector<std::string>& parts,
                             const std::string& indent, bool has_prefix) {
    std::vector<std::string> lines;
    std::vector<std::string> line;
    const int indent_length = Length(indent);
    int line_length = (has_prefix ? prefix_length : indent_length) - 1;
    for (const std::string& part : parts) {
      if (line_length + 1 + Length(part) > text_width && !line.empty()) {
        lines.push_back(indent + Join(line, " "));
        line.clear();
        line_length = indent_length - 1;
      }
      line.push_back(part);
      line_length += Length(part) + 1;
    }
    if (!line.empty()) lines.push_back(indent + Join(line, " "));
    if (has_prefix) lines[0].erase(0, indent.size());
    return lines;
  };
  const std::string prog(prog_);
  std::vector<std::string> lines;
  // A short prog is followed by the arguments, a long one is on its own.
  if (4 * (prefix_length + Length(prog)) <= 3 * text_width) {
    const std::string indent = Spaces(prefix_length + Length(prog) + 1);
    std::vector<std::string> first = {prog};
    if (!optionals.empty()) {
      first.insert(first.end(), optionals.begin(), optionals.end());
      lines = get_lines(first, indent, true);
      const std::vector<std::string> rest =
          get_lines(positionals, indent, false);
      lines.insert(lines.end(), rest.begin(), rest.end());
    } else {
      first.insert(first.end(), positionals.begin(), positionals.end());
      lines = get_lines(first, indent, true);
    }
  } else {
    const std::string indent = Spaces(prefix_length);
    lines = get_lines(all, indent, false);
    if (lines.size() > 1) {
      lines = get_lines(optionals, indent, false);
      const std::vector<std::string> rest =
          get_lines(positionals, indent, false);
      lines.insert(lines.end(), rest.begin(), rest.end());
    }
    lines.insert(lines.begin(), prog);
  }
  return std::format("{}{}\n\n", kPrefix, Join(lines, "\n"));
}

std::string HelpFormatter::Text(std::string_view text) const {
  if (text.empty()) return "";
  std::string expanded(text);
  if (expanded.find("%(prog)") != std::string::npos) {
    expanded = ExpandPercent(expanded, {{"prog", std::string(prog_)}});
  }
  if (format_ == HelpFormat::kRawDescription) return expanded + "\n\n";
  const int text_width = std::max(width_, 11);
  return Join(Wrap(NormalizeWhitespace(expanded), text_width), "\n") +
         "\n\n";
}

std::string HelpFormatter::Section(std::string_view heading,
                                   bool positional) const {
  std::string items;
  for (const Action& action : *actions_) {
    if (action.option_strings.empty() == positional) {
      items += FormatAction(action, 2);
    }
  }
  if (items.empty()) return "";
  return std::format("\n{}:\n{}\n", heading, items);
}

std::string HelpFormatter::Invocation(const Action& action) const {
  if (action.option_strings.empty()) return Metavar(action, action.dest);
  const std::string options = Join(action.option_strings, ", ");
  if (action.kind == Action::Kind::kHelp) return options;
  return options + " " + Metavar(action, Upper(action.dest));
}

std::string HelpFormatter::ExpandHelp(const Action& action) const {
  std::string help = action.help;
  if (format_ == HelpFormat::kArgumentDefaults &&
      help.find("%(default)") == std::string::npos &&
      action.default_value && !action.option_strings.empty()) {
    help += " (default: %(default)s)";
  }
  std::vector<std::pair<std::string_view, std::string>> params = {
      {"prog", std::string(prog_)}};
  if (!action.dest.empty()) params.emplace_back("dest", action.dest);
  if (action.default_value) {
    params.emplace_back("default", *action.default_value);
  }
  if (!action.choices.empty()) {
    params.emplace_back("choices", Join(action.choices, ", "));
  }
  return ExpandPercent(help, params);
}

std::string HelpFormatter::FormatAction(const Action& action,
                                        int indent) const {
  const int help_position =
      std::min(action_max_length_ + 2, max_help_position_);
  const int help_width = std::max(width_ - help_position, 11);
  const int action_width = help_position - indent - 2;
  const std::string invocation = Invocation(action);
  std::string result;
  int indent_first = 0;
  if (action.help.empty()) {
    result = Spaces(indent) + invocation + "\n";
  } else if (Length(invocation) <= action_width) {
    result = Spaces(indent) + LeftJustify(invocation, action_width) + "  ";
  } else {
    result = Spaces(indent) + invocation + "\n";
    indent_first = help_position;
  }
  const std::string help =
      action.help.empty() ? "" : NormalizeWhitespace(ExpandHelp(action));
  const std::vector<std::string> lines =
      help.empty() ? std::vector<std::string>() : Wrap(help, help_width);
  for (size_t i = 0; i < lines.size(); ++i) {
    result += Spaces(i == 0 ? indent_first : help_position) + lines[i] + "\n";
  }
  if (lines.empty() && !result.ends_with('\n')) result += "\n";
  for (const Action& subaction : action.subactions) {
    result += FormatAction(subaction, indent + 2);
  }
  return result;
}

std::string HelpFormatter::Finish(const std::string& help) {
  // Runs of blank lines become one, and the text ends with one newline.
  std::string result;
  int newlines = 0;
  for (const char c : help) {
    newlines = c == '\n' ? newlines + 1 : 0;
    if (newlines <= 2) result += c;
  }
  const size_t first = result.find_first_not_of('\n');
  if (first == std::string::npos) return "";
  return result.substr(first, result.find_last_not_of('\n') - first + 1) +
         "\n";
}

bool IsNegativeNumber(std::string_view arg) {
  // Python: ^-\d+$|^-\d*\.\d+$
  if (!arg.starts_with('-') || arg.size() < 2) return false;
  arg.remove_prefix(1);
  const size_t point = arg.find('.');
  const auto is_digits = [](std::string_view digits) {
    return digits.find_first_not_of("0123456789") == std::string_view::npos;
  };
  if (point == std::string_view::npos) return is_digits(arg);
  return is_digits(arg.substr(0, point)) && point + 1 < arg.size() &&
         is_digits(arg.substr(point + 1));
}

}  // namespace

bool Namespace::Has(std::string_view dest) const {
  return values_.find(dest) != values_.end();
}

const std::string& Namespace::Get(std::string_view dest) const {
  const auto value = values_.find(dest);
  if (value == values_.end()) std::terminate();
  return value->second;
}

void Namespace::Set(std::string_view dest, std::string value) {
  values_.insert_or_assign(std::string(dest), std::move(value));
}

void Namespace::Update(const Namespace& other) {
  for (const auto& [dest, value] : other.values_) {
    values_.insert_or_assign(dest, value);
  }
}

// One way to read an option string (Python: the option tuples of
// _parse_optional()).
struct Parser::OptionTuple {
  const Action* action;  // Null if no option of the parser matches.
  std::string option_string;
  // Whether the explicit argument came after "=" (Python: a truthy sep).
  bool equals_sign;
  // The value attached to the option string ("--output=json", "-ojson").
  std::optional<std::string> explicit_arg;
};

// The result of a parse that allows unknown arguments.
struct Parser::KnownArgs {
  Namespace values;
  std::vector<std::string> extras;
  // Arguments that a subcommand parser did not recognize.
  std::vector<std::string> unrecognized;
};

Parser::Parser(std::string prog) : prog_(std::move(prog)) {
  actions_.push_back({.kind = Action::Kind::kHelp,
                      .option_strings = {"-h", "--help"},
                      .help = "show this help message and exit"});
}

void Parser::AddArgument(Argument argument) {
  actions_.push_back({.kind = Action::Kind::kStore,
                      .option_strings = std::move(argument.flags),
                      .dest = std::move(argument.dest),
                      .choices = std::move(argument.choices),
                      .default_value = std::move(argument.default_value),
                      .help = std::move(argument.help)});
}

void Parser::SetDefault(std::string dest, std::string value) {
  defaults_.emplace_back(std::move(dest), std::move(value));
}

void Parser::AddSubparsers(std::string help) {
  subparsers_action_ = actions_.size();
  actions_.push_back(
      {.kind = Action::Kind::kSubparsers, .help = std::move(help)});
}

Parser& Parser::AddParser(std::string name, std::string help) {
  Action& action = actions_[subparsers_action_.value()];
  action.subactions.push_back({.dest = name, .help = std::move(help)});
  subparsers_.push_back(std::make_unique<Parser>(prog_ + " " + name));
  action.choices.push_back(std::move(name));
  return *subparsers_.back();
}

Result<Namespace> Parser::ParseArgs(
    const std::vector<std::string>& args) const {
  Result<KnownArgs> known = ParseKnownArgs(args);
  if (!known.ok()) return Result<Namespace>(known.error());
  if (!known.value().extras.empty()) {
    return Result<Namespace>(Fail(
        "unrecognized arguments: " + Join(known.value().extras, " ")));
  }
  return Result<Namespace>(std::move(known).value().values);
}

std::string Parser::FormatUsage() const {
  const HelpFormatter formatter(prog_, help_format_, &actions_);
  return HelpFormatter::Finish(formatter.Usage());
}

std::string Parser::FormatHelp() const {
  const HelpFormatter formatter(prog_, help_format_, &actions_);
  return HelpFormatter::Finish(
      formatter.Usage() + formatter.Text(description_) +
      formatter.Section("positional arguments", true) +
      formatter.Section("options", false) + formatter.Text(epilog_));
}

Result<Parser::KnownArgs> Parser::ParseKnownArgs(
    const std::vector<std::string>& args) const {
  KnownArgs known;
  for (const Action& action : actions_) {
    if (!action.dest.empty() && action.default_value &&
        !known.values.Has(action.dest)) {
      known.values.Set(action.dest, *action.default_value);
    }
  }
  for (const auto& [dest, value] : defaults_) {
    if (!known.values.Has(dest)) known.values.Set(dest, value);
  }

  // One letter per argument: 'O' for an option, 'A' for an argument, '-' for
  // "--", after which everything is an argument.
  std::string pattern;
  std::map<size_t, std::vector<OptionTuple>> options;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--") {
      pattern += '-';
      pattern.append(args.size() - i - 1, 'A');
      break;
    }
    std::optional<std::vector<OptionTuple>> tuples = ParseOptional(args[i]);
    pattern += tuples ? 'O' : 'A';
    if (tuples) options.emplace(i, std::move(*tuples));
  }

  // The subcommand argument is the only positional one. It takes everything
  // from the first argument on (nargs=PARSER, pattern -*A[-AO]*).
  bool positional_done = subparsers_action_ == std::nullopt;
  const auto consume_positionals = [&](size_t start) -> Result<size_t> {
    const size_t first = pattern.find_first_not_of('-', start);
    if (positional_done || first == std::string::npos ||
        pattern[first] != 'A') {
      return Result<size_t>(start);
    }
    positional_done = true;
    std::vector<std::string> values(args.begin() + start, args.end());
    if (pattern[start] == '-') values.erase(values.begin());
    if (std::optional<Error> error =
            TakeAction(actions_[*subparsers_action_], values, &known)) {
      return Result<size_t>(*error);
    }
    return Result<size_t>(args.size());
  };

  // Python: consume_optional().
  const auto consume_optional = [&](size_t start) -> Result<size_t> {
    const std::vector<OptionTuple>& tuples = options.at(start);
    if (tuples.size() > 1) {
      std::vector<std::string> matches;
      for (const OptionTuple& tuple : tuples) {
        matches.push_back(tuple.option_string);
      }
      return Result<size_t>(
          Fail(std::format("ambiguous option: {} could match {}", args[start],
                           Join(matches, ", "))));
    }
    OptionTuple tuple = tuples[0];
    std::vector<std::pair<const Action*, std::vector<std::string>>> calls;
    size_t stop = 0;
    while (true) {
      if (tuple.action == nullptr) {
        known.extras.push_back(args[start]);
        return Result<size_t>(start + 1);
      }
      const bool takes_value = tuple.action->kind != Action::Kind::kHelp;
      if (tuple.explicit_arg) {
        const std::string explicit_arg = *tuple.explicit_arg;
        if (!takes_value && tuple.option_string[1] != '-' &&
            !explicit_arg.empty()) {
          // A flag with more single-dash flags after it, as in -xyz.
          if (tuple.equals_sign || explicit_arg[0] == '-') {
            return Result<size_t>(Fail(ArgumentError(
                *tuple.action,
                "ignored explicit argument " + Repr(explicit_arg))));
          }
          calls.push_back({tuple.action, {}});
          const size_t first = CodePointPrefixSize(explicit_arg, 1);
          tuple.option_string =
              tuple.option_string.substr(0, 1) + explicit_arg.substr(0, first);
          tuple.action = FindOption(tuple.option_string);
          if (tuple.action == nullptr) {
            known.extras.push_back(tuple.option_string.substr(0, 1) +
                                   explicit_arg);
            stop = start + 1;
            break;
          }
          const std::string rest = explicit_arg.substr(first);
          tuple.equals_sign = rest.starts_with('=');
          tuple.explicit_arg = rest.empty() ? std::nullopt
                               : tuple.equals_sign
                                   ? std::optional<std::string>(rest.substr(1))
                                   : std::optional<std::string>(rest);
        } else if (takes_value) {
          stop = start + 1;
          calls.push_back({tuple.action, {explicit_arg}});
          break;
        } else {
          return Result<size_t>(Fail(ArgumentError(
              *tuple.action,
              "ignored explicit argument " + Repr(explicit_arg))));
        }
      } else {
        const size_t begin = start + 1;
        if (takes_value && (begin >= pattern.size() || pattern[begin] != 'A')) {
          return Result<size_t>(
              Fail(ArgumentError(*tuple.action, "expected one argument")));
        }
        stop = begin + (takes_value ? 1 : 0);
        calls.push_back({tuple.action, std::vector<std::string>(
                                           args.begin() + begin,
                                           args.begin() + stop)});
        break;
      }
    }
    for (const auto& [action, values] : calls) {
      if (std::optional<Error> error = TakeAction(*action, values, &known)) {
        return Result<size_t>(*error);
      }
    }
    return Result<size_t>(stop);
  };

  // Positionals and options are consumed in turn until the last option.
  size_t start = 0;
  const size_t end = options.empty() ? 0 : options.rbegin()->first + 1;
  while (start < end) {
    size_t next_option = start;
    while (next_option < end && !options.contains(next_option)) ++next_option;
    if (start != next_option) {
      const Result<size_t> positionals_end = consume_positionals(start);
      if (!positionals_end.ok()) {
        return Result<KnownArgs>(positionals_end.error());
      }
      const bool consumed = positionals_end.value() > start;
      start = positionals_end.value();
      if (consumed) continue;
    }
    if (!options.contains(start)) {
      known.extras.insert(known.extras.end(), args.begin() + start,
                          args.begin() + next_option);
      start = next_option;
    }
    const Result<size_t> next = consume_optional(start);
    if (!next.ok()) return Result<KnownArgs>(next.error());
    start = next.value();
  }
  const Result<size_t> stop = consume_positionals(start);
  if (!stop.ok()) return Result<KnownArgs>(stop.error());
  known.extras.insert(known.extras.end(), args.begin() + stop.value(),
                      args.end());
  known.extras.insert(known.extras.end(), known.unrecognized.begin(),
                      known.unrecognized.end());
  known.unrecognized.clear();
  return Result<KnownArgs>(std::move(known));
}

std::optional<std::vector<Parser::OptionTuple>> Parser::ParseOptional(
    const std::string& arg) const {
  if (!arg.starts_with('-')) return std::nullopt;
  if (const Action* action = FindOption(arg)) {
    return std::vector<OptionTuple>{{action, arg, false, std::nullopt}};
  }
  if (arg.size() == 1) return std::nullopt;
  if (const size_t equals = arg.find('='); equals != std::string::npos) {
    const std::string option_string = arg.substr(0, equals);
    if (const Action* action = FindOption(option_string)) {
      return std::vector<OptionTuple>{
          {action, option_string, true, arg.substr(equals + 1)}};
    }
  }
  std::vector<OptionTuple> tuples = OptionTuples(arg);
  if (!tuples.empty()) return tuples;
  // No option of this parser looks like a negative number, so a negative
  // number is a positional argument.
  if (IsNegativeNumber(arg)) return std::nullopt;
  if (arg.find(' ') != std::string::npos) return std::nullopt;
  return std::vector<OptionTuple>{{nullptr, arg, false, std::nullopt}};
}

std::vector<Parser::OptionTuple> Parser::OptionTuples(
    const std::string& arg) const {
  const size_t equals = arg.find('=');
  const std::string prefix = arg.substr(0, equals);
  const std::optional<std::string> explicit_arg =
      equals == std::string::npos
          ? std::nullopt
          : std::optional<std::string>(arg.substr(equals + 1));
  const bool long_option = arg[1] == '-';
  std::vector<OptionTuple> tuples;
  for (const Action& action : actions_) {
    for (const std::string& option_string : action.option_strings) {
      if (!long_option && option_string == arg.substr(0, 2)) {
        // A short option with its value attached, as in -ojson.
        tuples.push_back({&action, option_string, false, arg.substr(2)});
      } else if (allow_abbrev_ && option_string.starts_with(prefix)) {
        tuples.push_back({&action, option_string,
                          equals != std::string::npos, explicit_arg});
      }
    }
  }
  return tuples;
}

const internal_argparse::Action* Parser::FindOption(
    std::string_view option_string) const {
  for (const Action& action : actions_) {
    if (std::ranges::find(action.option_strings, option_string) !=
        action.option_strings.end()) {
      return &action;
    }
  }
  return nullptr;
}

std::optional<Error> Parser::TakeAction(const Action& action,
                                        const std::vector<std::string>& values,
                                        KnownArgs* known) const {
  if (action.kind == Action::Kind::kHelp) {
    Write(Stream::kStdout, FormatHelp());
    return Error{.exit_code = 0};
  }
  const std::string& value = values.front();
  if (!action.choices.empty() &&
      std::ranges::find(action.choices, value) == action.choices.end()) {
    return Fail(ArgumentError(
        action, std::format("invalid choice: {} (choose from {})", Repr(value),
                            Join(action.choices, ", "))));
  }
  if (action.kind == Action::Kind::kStore) {
    known->values.Set(action.dest, value);
    return std::nullopt;
  }
  const size_t index = std::ranges::find(action.choices, value) -
                       action.choices.begin();
  const Result<KnownArgs> sub = subparsers_[index]->ParseKnownArgs(
      std::vector<std::string>(values.begin() + 1, values.end()));
  if (!sub.ok()) return sub.error();
  known->values.Update(sub.value().values);
  known->unrecognized.insert(known->unrecognized.end(),
                             sub.value().extras.begin(),
                             sub.value().extras.end());
  return std::nullopt;
}

Error Parser::Fail(std::string_view message) const {
  Write(Stream::kStderr, FormatUsage());
  Write(Stream::kStderr, std::format("{}: error: {}\n", prog_, message));
  return Error{.exit_code = 1};
}

}  // namespace cc::util::argparse
