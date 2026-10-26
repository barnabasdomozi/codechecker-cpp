#include "util/template.h"

#include <stddef.h>

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::util {
namespace {

bool IsNameStart(char c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsNameChar(char c) { return IsNameStart(c) || (c >= '0' && c <= '9'); }

// Returns the length of the name at the start of `text`; 0 if there is none.
size_t NameLength(std::string_view text) {
  if (text.empty() || !IsNameStart(text[0])) return 0;
  size_t length = 1;
  while (length < text.size() && IsNameChar(text[length])) ++length;
  return length;
}

// Returns the error of Python's Template for the invalid "$" at `pos` of
// `text`. Python gives the position just after the "$": the line, and the
// column in code points, both from 1. Only "\n" ends a line here; Python
// also counts the other line ends of str.splitlines().
Error InvalidPlaceholder(std::string_view text, size_t pos) {
  const std::string_view before = text.substr(0, pos + 1);
  const size_t newline = before.rfind('\n');
  const size_t line_start = newline == std::string_view::npos ? 0 : newline + 1;
  return Error{.message = std::format(
                   "ValueError: Invalid placeholder in string: line {}, col {}",
                   std::ranges::count(before, '\n') + 1,
                   CodePointCount(before.substr(line_start)))};
}

}  // namespace

Result<std::string> SubstituteTemplate(
    std::string_view text,
    const std::map<std::string, std::string, std::less<>>& values) {
  std::string result;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t dollar = text.find('$', pos);
    if (dollar == std::string_view::npos) break;
    result += text.substr(pos, dollar - pos);
    const std::string_view rest = text.substr(dollar + 1);
    if (rest.starts_with('$')) {
      result += '$';
      pos = dollar + 2;
      continue;
    }
    const bool braced = rest.starts_with('{');
    const size_t length = NameLength(rest.substr(braced ? 1 : 0));
    if (length == 0 || (braced && rest.substr(1 + length, 1) != "}")) {
      return Result<std::string>(InvalidPlaceholder(text, dollar));
    }
    const std::string_view name = rest.substr(braced ? 1 : 0, length);
    const auto value = values.find(name);
    if (value == values.end()) {
      return Result<std::string>(
          Error{.message = "KeyError: " + Repr(name)});
    }
    result += value->second;
    pos = dollar + 1 + length + (braced ? 2 : 0);
  }
  result += text.substr(std::min(pos, text.size()));
  return Result<std::string>(std::move(result));
}

}  // namespace cc::util
