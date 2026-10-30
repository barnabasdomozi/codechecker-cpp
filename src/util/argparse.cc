#include "util/argparse.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <charconv>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "util/print.h"
#include "util/pyre.h"
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

// The number of arguments an action takes, in the terms of Python's nargs.
enum class NargsKind {
  kZero,
  kOne,
  kZeroOrOne,
  kZeroOrMore,
  kOneOrMore,
  kParser
};

NargsKind NargsOf(const Action& action) {
  switch (action.kind) {
    case Action::Kind::kHelp:
    case Action::Kind::kStoreTrue:
    case Action::Kind::kStoreConst:
      return NargsKind::kZero;
    case Action::Kind::kSubparsers:
      return NargsKind::kParser;
    case Action::Kind::kStore:
      break;
  }
  switch (action.nargs) {
    case Nargs::kZeroOrOne:
      return NargsKind::kZeroOrOne;
    case Nargs::kZeroOrMore:
      return NargsKind::kZeroOrMore;
    case Nargs::kOneOrMore:
      return NargsKind::kOneOrMore;
    case Nargs::kOne:
      break;
  }
  return NargsKind::kOne;
}

// Python: _get_nargs_pattern().
std::string_view NargsPattern(const Action& action) {
  const bool option = !action.option_strings.empty();
  switch (NargsOf(action)) {
    case NargsKind::kZero:
      return option ? "([AO]{0})" : "((?:-*A){0}-*)";
    case NargsKind::kOne:
      return option ? "([A])" : "(-*A-*)";
    case NargsKind::kZeroOrOne:
      return option ? "(A?)" : "(-*A?-*)";
    case NargsKind::kZeroOrMore:
      return option ? "(A*)" : "(-*[A-]*)";
    case NargsKind::kOneOrMore:
      return option ? "(A+)" : "(-*A[A-]*)";
    case NargsKind::kParser:
      return option ? "(A[AO]*)" : "(-*A[-AO]*)";
  }
  return "";
}

// Returns the metavar argparse shows for `action`: its metavar, else its
// choices in braces, else `default_metavar` (Python: _metavar_formatter()).
std::string Metavar(const Action& action, std::string_view default_metavar) {
  if (!action.metavar.empty()) return action.metavar;
  if (!action.choices.empty()) return "{" + Join(action.choices, ",") + "}";
  return std::string(default_metavar);
}

// Python: _format_args().
std::string FormatArgs(const Action& action, std::string_view default_metavar) {
  const std::string metavar = Metavar(action, default_metavar);
  switch (NargsOf(action)) {
    case NargsKind::kZero:
      return "";
    case NargsKind::kOne:
      return metavar;
    case NargsKind::kZeroOrOne:
      return "[" + metavar + "]";
    case NargsKind::kZeroOrMore:
      return "[" + metavar + " ...]";
    case NargsKind::kOneOrMore:
      return metavar + " [" + metavar + " ...]";
    case NargsKind::kParser:
      return metavar + " ...";
  }
  return metavar;
}

// The default shown in help texts: nullopt for argparse.SUPPRESS.
std::optional<std::string> HelpDefault(const Action& action) {
  if (action.default_none) return "None";
  if (action.default_list) {
    std::vector<std::string> items;
    for (const std::string& item : *action.default_list) {
      items.push_back(Repr(item));
    }
    return "[" + Join(items, ", ") + "]";
  }
  return action.default_value;
}

// The name of `action` in error messages (Python: _get_action_name()).
std::string ActionName(const Action& action) {
  if (!action.option_strings.empty()) return Join(action.option_strings, "/");
  if (!action.metavar.empty()) return action.metavar;
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
  // `actions` and `required_groups`, the ids of the required mutually
  // exclusive groups, must outlive the formatter.
  HelpFormatter(std::string_view prog, HelpFormat format,
                const std::vector<Action>* actions,
                const std::vector<int>* required_groups);

  HelpFormatter(const HelpFormatter&) = delete;
  HelpFormatter& operator=(const HelpFormatter&) = delete;

  // Returns "usage: ...\n\n" (Python: _format_usage()).
  std::string Usage() const;
  // Returns a description or an epilog indented by `indent`, followed by a
  // blank line, or nothing for an empty text (Python: _format_text()).
  std::string Text(std::string_view text, int indent) const;
  // Returns a section of argument help: a heading, the description of the
  // group and the arguments for which `in_section` is true, or nothing if
  // there are no arguments.
  std::string Section(std::string_view heading, std::string_view description,
                      const std::function<bool(const Action&)>& in_section)
      const;

  // Returns the final text from the pieces above (Python: format_help()).
  static std::string Finish(const std::string& help);

 private:
  bool RawText() const {
    return format_ == HelpFormat::kRawDescription ||
           format_ == HelpFormat::kRawDescriptionArgumentDefaults ||
           format_ == HelpFormat::kCodeChecker;
  }
  bool ShowDefaults() const {
    return format_ == HelpFormat::kArgumentDefaults ||
           format_ == HelpFormat::kRawDescriptionArgumentDefaults ||
           format_ == HelpFormat::kCodeChecker;
  }
  // Python: _split_lines() of the formatter.
  std::vector<std::string> SplitLines(const std::string& help,
                                      int width) const;
  std::vector<std::string> UsageParts(bool positional) const;
  std::string Invocation(const Action& action) const;
  std::string ExpandHelp(const Action& action) const;
  // Python: _format_action().
  std::string FormatAction(const Action& action, int indent) const;

  std::string_view prog_;
  HelpFormat format_;
  const std::vector<Action>* actions_;
  const std::vector<int>* required_groups_;
  int width_;
  int max_help_position_;
  // The longest invocation plus its indent (Python: _action_max_length).
  int action_max_length_ = 0;
};

HelpFormatter::HelpFormatter(std::string_view prog, HelpFormat format,
                             const std::vector<Action>* actions,
                             const std::vector<int>* required_groups)
    : prog_(prog),
      format_(format),
      actions_(actions),
      required_groups_(required_groups),
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
  std::vector<const Action*> actions;
  for (const Action& action : *actions_) {
    if (action.option_strings.empty() == positional) actions.push_back(&action);
  }
  std::vector<std::string> parts;
  for (const Action* action : actions) {
    if (positional) {
      parts.push_back(FormatArgs(*action, action->dest));
      continue;
    }
    std::string part = action->option_strings[0];
    if (NargsOf(*action) != NargsKind::kZero) {
      part += " " + FormatArgs(*action, Upper(action->dest));
    }
    // Options of a mutually exclusive group get brackets as a group.
    const bool bracket = !action->required && action->exclusive_group == 0;
    parts.push_back(bracket ? "[" + part + "]" : part);
  }
  // Python: the group part of _get_actions_usage_parts(), for groups of
  // consecutive options. A required group of one option has no brackets.
  for (size_t start = 0; start < actions.size(); ++start) {
    const int group = actions[start]->exclusive_group;
    if (group == 0 ||
        (start > 0 && actions[start - 1]->exclusive_group == group)) {
      continue;
    }
    size_t end = start;
    while (end < actions.size() && actions[end]->exclusive_group == group) {
      ++end;
    }
    std::string_view open = "[";
    std::string_view close = "]";
    if (std::ranges::find(*required_groups_, group) !=
        required_groups_->end()) {
      open = end - start > 1 ? "(" : "";
      close = end - start > 1 ? ")" : "";
    }
    parts[start] = std::string(open) + parts[start];
    parts[end - 1] += close;
    for (size_t i = start; i + 1 < end; ++i) parts[i] += " |";
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

std::string HelpFormatter::Text(std::string_view text, int indent) const {
  if (text.empty()) return "";
  std::string expanded(text);
  if (expanded.find("%(prog)") != std::string::npos) {
    expanded = ExpandPercent(expanded, {{"prog", std::string(prog_)}});
  }
  const std::string spaces = Spaces(indent);
  if (RawText()) {
    std::string result;
    for (const std::string_view line : util::SplitLines(expanded)) {
      result += spaces;
      result += line;
    }
    return result + "\n\n";
  }
  const int text_width = std::max(width_ - indent, 11);
  return Join(Wrap(NormalizeWhitespace(expanded), text_width, spaces), "\n") +
         "\n\n";
}

std::string HelpFormatter::Section(
    std::string_view heading, std::string_view description,
    const std::function<bool(const Action&)>& in_section) const {
  std::string items = Text(description, 2);
  for (const Action& action : *actions_) {
    if (in_section(action)) items += FormatAction(action, 2);
  }
  if (items.empty()) return "";
  return std::format("\n{}:\n{}\n", heading, items);
}

std::string HelpFormatter::Invocation(const Action& action) const {
  if (action.option_strings.empty()) return Metavar(action, action.dest);
  const std::string options = Join(action.option_strings, ", ");
  if (NargsOf(action) == NargsKind::kZero) return options;
  return options + " " + FormatArgs(action, Upper(action.dest));
}

std::string HelpFormatter::ExpandHelp(const Action& action) const {
  std::string help = action.help;
  const std::optional<std::string> default_value = HelpDefault(action);
  const NargsKind nargs = NargsOf(action);
  if (ShowDefaults() && help.find("%(default)") == std::string::npos &&
      default_value &&
      (!action.option_strings.empty() || nargs == NargsKind::kZeroOrOne ||
       nargs == NargsKind::kZeroOrMore)) {
    help += " (default: %(default)s)";
  }
  std::vector<std::pair<std::string_view, std::string>> params = {
      {"prog", std::string(prog_)}};
  if (!action.dest.empty()) params.emplace_back("dest", action.dest);
  if (default_value) params.emplace_back("default", *default_value);
  if (!action.metavar.empty()) params.emplace_back("metavar", action.metavar);
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
  const std::vector<std::string> lines =
      action.help.empty() ? std::vector<std::string>()
                          : SplitLines(ExpandHelp(action), help_width);
  for (size_t i = 0; i < lines.size(); ++i) {
    result += Spaces(i == 0 ? indent_first : help_position) + lines[i] + "\n";
  }
  if (lines.empty() && !result.ends_with('\n')) result += "\n";
  for (const Action& subaction : action.subactions) {
    result += FormatAction(subaction, indent + 2);
  }
  return result;
}

std::vector<std::string> HelpFormatter::SplitLines(const std::string& help,
                                                   int width) const {
  if (format_ == HelpFormat::kCodeChecker && help.starts_with("R|")) {
    // The lines are kept, and each is wrapped on its own.
    std::vector<std::string> lines;
    std::string_view text = std::string_view(help).substr(2);
    text = text.substr(
        std::min(text.size(), text.find_first_not_of(" \t\n\r\f\v")));
    for (const std::string_view line : StrSplitLines(text)) {
      for (std::string& part : Wrap(line, width)) {
        lines.push_back(std::move(part));
      }
    }
    return lines;
  }
  const std::string text = NormalizeWhitespace(help);
  return text.empty() ? std::vector<std::string>() : Wrap(text, width);
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

// Returns how many of the arguments at the start of `pattern` each of the
// first positionals takes, matching as many positionals as possible
// (Python: _match_arguments_partial()).
std::vector<size_t> MatchArgumentsPartial(
    const std::vector<const Action*>& actions, std::string_view pattern) {
  for (size_t count = actions.size(); count > 0; --count) {
    std::string regex;
    for (size_t i = 0; i < count; ++i) regex += NargsPattern(*actions[i]);
    const Result<Regex> compiled = Regex::Compile(regex);
    if (!compiled.ok()) std::terminate();
    const std::optional<RegexMatch> match = compiled.value().Match(pattern);
    if (!match) continue;
    std::vector<size_t> result;
    for (int group = 1; group <= match->GroupCount(); ++group) {
      result.push_back(match->Group(group).value_or("").size());
    }
    if (match->End() < pattern.size() && pattern[match->End()] == 'O') {
      while (!result.empty() && result.back() == 0) result.pop_back();
    }
    return result;
  }
  return {};
}

}  // namespace

bool Namespace::Has(std::string_view dest) const {
  return values_.find(dest) != values_.end();
}

const std::string& Namespace::Get(std::string_view dest) const {
  const std::vector<std::string>& values = GetList(dest);
  if (values.size() != 1) std::terminate();
  return values.front();
}

const std::vector<std::string>& Namespace::GetList(
    std::string_view dest) const {
  const auto value = values_.find(dest);
  if (value == values_.end()) std::terminate();
  return value->second;
}

void Namespace::Set(std::string_view dest, std::string value) {
  SetList(dest, {std::move(value)});
}

void Namespace::SetList(std::string_view dest,
                        std::vector<std::string> values) {
  values_.insert_or_assign(std::string(dest), std::move(values));
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
  Action::Kind kind = Action::Kind::kStore;
  if (argument.action == ArgumentAction::kStoreTrue) {
    kind = Action::Kind::kStoreTrue;
  } else if (argument.action == ArgumentAction::kStoreConst) {
    kind = Action::Kind::kStoreConst;
  }
  // Python makes positionals required unless nargs is '?' or '*'.
  const bool required =
      argument.required ||
      (argument.flags.empty() && argument.nargs != Nargs::kZeroOrOne &&
       argument.nargs != Nargs::kZeroOrMore);
  actions_.push_back({.kind = kind,
                      .option_strings = std::move(argument.flags),
                      .dest = std::move(argument.dest),
                      .nargs = argument.nargs,
                      .metavar = std::move(argument.metavar),
                      .choices = std::move(argument.choices),
                      .default_value = std::move(argument.default_value),
                      .default_list = std::move(argument.default_list),
                      .default_none = argument.default_none,
                      .required = required,
                      .help = std::move(argument.help),
                      .const_value = std::move(argument.const_value),
                      .type = std::move(argument.type),
                      .custom_action = std::move(argument.custom_action),
                      .group = argument.group,
                      .exclusive_group = argument.exclusive_group});
}

int Parser::AddArgumentGroup(std::string title, std::string description) {
  groups_.push_back(
      {.title = std::move(title), .description = std::move(description)});
  return static_cast<int>(groups_.size());
}

int Parser::AddMutuallyExclusiveGroup(ExclusiveGroup kind) {
  ++exclusive_groups_;
  if (kind == ExclusiveGroup::kRequired) {
    required_exclusive_groups_.push_back(exclusive_groups_);
  }
  return exclusive_groups_;
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
  subparsers_.back()->set_error_exit_code(error_exit_code_);
  subparsers_.back()->set_help_banner(help_banner_);
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
  const HelpFormatter formatter(prog_, help_format_, &actions_,
                                &required_exclusive_groups_);
  return HelpFormatter::Finish(formatter.Usage());
}

std::string Parser::FormatHelp() const {
  const HelpFormatter formatter(prog_, help_format_, &actions_,
                                &required_exclusive_groups_);
  std::string help = formatter.Usage() + formatter.Text(description_, 0) +
                     formatter.Section("positional arguments", "",
                                       [](const Action& action) {
                                         return action.group == 0 &&
                                                action.option_strings.empty();
                                       }) +
                     formatter.Section("options", "", [](const Action& action) {
                       return action.group == 0 &&
                              !action.option_strings.empty();
                     });
  for (size_t i = 0; i < groups_.size(); ++i) {
    const int group = static_cast<int>(i) + 1;
    help += formatter.Section(groups_[i].title, groups_[i].description,
                              [group](const Action& action) {
                                return action.group == group;
                              });
  }
  return HelpFormatter::Finish(help + formatter.Text(epilog_, 0));
}

Result<Parser::KnownArgs> Parser::ParseKnownArgs(
    const std::vector<std::string>& args) const {
  KnownArgs known;
  for (const Action& action : actions_) {
    if (action.dest.empty() || known.values.Has(action.dest)) continue;
    if (action.default_list) {
      known.values.SetList(action.dest, *action.default_list);
    } else if (action.default_value) {
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

  std::set<const Action*> seen_actions;
  const auto take_action =
      [&](const Action& action,
          const std::vector<std::string>& values) -> std::optional<Error> {
    seen_actions.insert(&action);
    // An option of a mutually exclusive group conflicts with the others.
    if (action.exclusive_group != 0) {
      for (const Action* seen : seen_actions) {
        if (seen != &action &&
            seen->exclusive_group == action.exclusive_group) {
          return Fail(ArgumentError(
              action, "not allowed with argument " + ActionName(*seen)));
        }
      }
    }
    return TakeAction(action, values, &known);
  };

  // The positionals not consumed yet.
  std::vector<const Action*> positionals;
  for (const Action& action : actions_) {
    if (action.option_strings.empty()) positionals.push_back(&action);
  }

  // Python: consume_positionals().
  const auto consume_positionals = [&](size_t start) -> Result<size_t> {
    const std::vector<size_t> counts = MatchArgumentsPartial(
        positionals, std::string_view(pattern).substr(start));
    for (size_t i = 0; i < counts.size(); ++i) {
      const Action& action = *positionals[i];
      std::vector<std::string> values(args.begin() + start,
                                      args.begin() + start + counts[i]);
      const bool has_dashes =
          NargsOf(action) == NargsKind::kParser
              ? pattern[start] == '-'
              : std::string_view(pattern).substr(start, counts[i]).find('-') !=
                    std::string_view::npos;
      if (has_dashes) values.erase(std::ranges::find(values, "--"));
      start += counts[i];
      if (std::optional<Error> error = take_action(action, values)) {
        return Result<size_t>(*error);
      }
    }
    positionals.erase(positionals.begin(), positionals.begin() + counts.size());
    return Result<size_t>(start);
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
      if (tuple.explicit_arg) {
        const std::string explicit_arg = *tuple.explicit_arg;
        const Result<size_t> count = MatchArgument(*tuple.action, "A");
        if (!count.ok()) return count;
        if (count.value() == 0 && tuple.option_string[1] != '-' &&
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
        } else if (count.value() == 1) {
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
        const Result<size_t> count = MatchArgument(
            *tuple.action, std::string_view(pattern).substr(begin));
        if (!count.ok()) return count;
        stop = begin + count.value();
        calls.push_back({tuple.action, std::vector<std::string>(
                                           args.begin() + begin,
                                           args.begin() + stop)});
        break;
      }
    }
    for (const auto& [action, values] : calls) {
      if (std::optional<Error> error = take_action(*action, values)) {
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

  std::vector<std::string> missing;
  for (const Action& action : actions_) {
    if (action.required && !seen_actions.contains(&action)) {
      missing.push_back(ActionName(action));
    }
  }
  if (!missing.empty()) {
    return Result<KnownArgs>(Fail("the following arguments are required: " +
                                  Join(missing, ", ")));
  }
  for (const int group : required_exclusive_groups_) {
    std::vector<std::string> names;
    bool given = false;
    for (const Action& action : actions_) {
      if (action.exclusive_group != group) continue;
      names.push_back(ActionName(action));
      given = given || seen_actions.contains(&action);
    }
    if (!given) {
      return Result<KnownArgs>(Fail("one of the arguments " +
                                    Join(names, " ") + " is required"));
    }
  }
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

Result<size_t> Parser::MatchArgument(const Action& action,
                                     std::string_view pattern) const {
  const Result<Regex> regex = Regex::Compile(NargsPattern(action));
  if (!regex.ok()) std::terminate();
  if (const std::optional<RegexMatch> match = regex.value().Match(pattern)) {
    return Result<size_t>(match->Group(1).value_or("").size());
  }
  std::string_view message = "expected one argument";
  if (NargsOf(action) == NargsKind::kOneOrMore) {
    message = "expected at least one argument";
  }
  return Result<size_t>(Fail(ArgumentError(action, message)));
}

std::optional<Error> Parser::TakeAction(const Action& action,
                                        const std::vector<std::string>& values,
                                        KnownArgs* known) const {
  if (action.kind == Action::Kind::kHelp) {
    Write(Stream::kStdout, help_banner_ + FormatHelp());
    return Error{.exit_code = 0};
  }
  if (action.kind == Action::Kind::kStoreTrue ||
      action.kind == Action::Kind::kStoreConst) {
    known->values.Set(action.dest, action.kind == Action::Kind::kStoreTrue
                                       ? "True"
                                       : action.const_value);
    return std::nullopt;
  }
  // Python's _get_values(): PARSER checks only the subcommand name.
  const size_t checked =
      action.kind == Action::Kind::kSubparsers ? 1 : values.size();
  std::vector<std::string> converted = values;
  for (size_t i = 0; i < checked; ++i) {
    if (action.type) {
      Result<std::string> value = action.type(values[i]);
      if (!value.ok()) {
        return Fail(ArgumentError(action, value.error().message));
      }
      converted[i] = std::move(value).value();
    }
    if (!action.choices.empty() &&
        std::ranges::find(action.choices, converted[i]) ==
            action.choices.end()) {
      return Fail(ArgumentError(
          action, std::format("invalid choice: {} (choose from {})",
                              Repr(converted[i]), Join(action.choices, ", "))));
    }
  }
  if (action.kind == Action::Kind::kStore) {
    std::vector<std::string> stored = converted;
    // Python's _get_values(): a positional argument with nargs '*' that got
    // no values takes its default.
    if (values.empty() && action.option_strings.empty() &&
        NargsOf(action) == NargsKind::kZeroOrMore) {
      if (action.default_list) {
        stored = *action.default_list;
      } else if (action.default_value) {
        stored = {*action.default_value};
      }
    }
    // Python's _get_values(): an option with nargs '?' given without a
    // value stores its const, and such a positional argument its default.
    if (values.empty() && NargsOf(action) == NargsKind::kZeroOrOne) {
      if (!action.option_strings.empty()) {
        stored = {action.const_value};
      } else if (action.default_value) {
        stored = {*action.default_value};
      } else {
        return std::nullopt;
      }
    }
    if (action.custom_action) {
      const std::vector<std::string>* previous =
          known->values.Has(action.dest) ? &known->values.GetList(action.dest)
                                         : nullptr;
      Result<std::vector<std::string>> result =
          action.custom_action(converted, previous);
      if (!result.ok()) return result.error();
      stored = std::move(result).value();
    }
    const NargsKind nargs = NargsOf(action);
    if ((nargs == NargsKind::kOne || nargs == NargsKind::kZeroOrOne) &&
        !action.custom_action) {
      known->values.Set(action.dest, stored.front());
    } else {
      known->values.SetList(action.dest, std::move(stored));
    }
    return std::nullopt;
  }
  const size_t index = std::ranges::find(action.choices, values.front()) -
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

Result<std::string> IntType(const std::string& value) {
  const std::optional<int64_t> number = ParseInt(value);
  if (!number) {
    return Result<std::string>(
        Error{.message = "invalid int value: " + Repr(value)});
  }
  return Result<std::string>(std::to_string(*number));
}

Result<std::string> FloatType(const std::string& value) {
  std::string text(Strip(value));
  std::erase(text, '_');
  const std::string lower = AsciiLower(text);
  const std::string_view unsigned_text =
      std::string_view(lower).substr(lower.starts_with('+') ||
                                             lower.starts_with('-')
                                         ? 1
                                         : 0);
  bool valid = unsigned_text == "inf" || unsigned_text == "infinity" ||
               unsigned_text == "nan";
  if (!valid && !text.empty()) {
    double number = 0;
    const char* begin = text.data() + (text[0] == '+' ? 1 : 0);
    const std::from_chars_result result =
        std::from_chars(begin, text.data() + text.size(), number);
    valid = result.ec == std::errc() && result.ptr == text.data() + text.size();
  }
  if (!valid) {
    return Result<std::string>(
        Error{.message = "invalid float value: " + Repr(value)});
  }
  return Result<std::string>(value);
}

Error Parser::Fail(std::string_view message) const {
  Write(Stream::kStderr, FormatUsage());
  Write(Stream::kStderr, std::format("{}: error: {}\n", prog_, message));
  return Error{.exit_code = error_exit_code_};
}

}  // namespace cc::util::argparse
