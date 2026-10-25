#include "util/strings.h"

#include <stddef.h>
#include <stdint.h>

#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "util/unicode.h"

namespace cc::util {
namespace {

constexpr std::string_view kAsciiWhitespace = " \t\n\v\f\r";

}  // namespace

std::string Repr(std::string_view text) {
  const bool double_quotes = text.find('\'') != std::string_view::npos &&
                             text.find('"') == std::string_view::npos;
  const char quote = double_quotes ? '"' : '\'';
  std::string result(1, quote);
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == quote || c == '\\') {
      result += '\\';
      result += c;
    } else if (c == '\n') {
      result += "\\n";
    } else if (c == '\r') {
      result += "\\r";
    } else if (c == '\t') {
      result += "\\t";
    } else if (byte < 0x20 || byte == 0x7F) {
      result += std::format("\\x{:02x}", byte);
    } else {
      // Printable ASCII and all non-ASCII characters. repr() also escapes
      // non-printable non-ASCII characters; no caller needs that yet.
      result += c;
    }
  }
  result += quote;
  return result;
}

std::optional<int64_t> ParseInt(std::string_view text) {
  text = StripAsciiWhitespace(text);
  if (text.starts_with('+')) {
    text.remove_prefix(1);
    if (text.starts_with('-')) return std::nullopt;
  }
  if (text.empty() || text.front() == '+') return std::nullopt;
  int64_t value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result result =
      std::from_chars(text.data(), end, value);
  if (result.ec != std::errc() || result.ptr != end) return std::nullopt;
  return value;
}

std::string_view StripAsciiWhitespace(std::string_view text) {
  const size_t first = text.find_first_not_of(kAsciiWhitespace);
  if (first == std::string_view::npos) return {};
  const size_t last = text.find_last_not_of(kAsciiWhitespace);
  return text.substr(first, last - first + 1);
}

std::string ExpandTabs(std::string_view text) {
  constexpr int kTabSize = 8;
  std::string result;
  int column = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t start = pos;
    NextCodePoint(text, &pos);
    const char c = text[start];
    if (c == '\t') {
      const int spaces = kTabSize - column % kTabSize;
      result.append(spaces, ' ');
      column += spaces;
      continue;
    }
    result.append(text.substr(start, pos - start));
    column = (c == '\n' || c == '\r') ? 0 : column + 1;
  }
  return result;
}

std::string LeftJustify(std::string_view text, int width) {
  std::string result(text);
  const int length = static_cast<int>(CodePointCount(text));
  if (length < width) result.append(width - length, ' ');
  return result;
}

std::string Join(const std::vector<std::string>& parts,
                 std::string_view separator) {
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) result += separator;
    result += parts[i];
  }
  return result;
}

}  // namespace cc::util
