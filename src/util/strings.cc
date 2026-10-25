#include "util/strings.h"

#include <stddef.h>
#include <stdint.h>

#include <charconv>
#include <cmath>
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

// Returns the byte offset of the first character of `text` that is not
// whitespace, or text.size().
size_t FirstNonSpace(std::string_view text) {
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t start = pos;
    const std::optional<uint32_t> c = NextCodePoint(text, &pos);
    if (!c || !IsSpace(*c)) return start;
  }
  return text.size();
}

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

std::string_view Strip(std::string_view text) {
  return RightStrip(text.substr(FirstNonSpace(text)));
}

std::string_view RightStrip(std::string_view text) {
  size_t end = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    const std::optional<uint32_t> c = NextCodePoint(text, &pos);
    if (!c || !IsSpace(*c)) end = pos;
  }
  return text.substr(0, end);
}

std::string RemoveWhitespace(std::string_view text) {
  std::string result;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t start = pos;
    const std::optional<uint32_t> c = NextCodePoint(text, &pos);
    if (!c || !IsSpace(*c)) result.append(text.substr(start, pos - start));
  }
  return result;
}

std::string TranslateNewlines(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\r') {
      result += text[i];
      continue;
    }
    result += '\n';
    if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
  }
  return result;
}

std::vector<std::string_view> SplitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n' && text[i] != '\r') continue;
    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
    lines.push_back(text.substr(start, i + 1 - start));
    start = i + 1;
  }
  if (start < text.size()) lines.push_back(text.substr(start));
  return lines;
}

std::string FloatRepr(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
  std::string out = std::signbit(value) ? "-" : "";
  if (value == 0) return out + "0.0";
  // The shortest digits that read back to the same value, in positional
  // notation if the decimal point is at most 16 digits right or 4 digits
  // left of the first digit, else in exponent notation.
  char buffer[64];
  const std::to_chars_result result =
      std::to_chars(buffer, buffer + sizeof(buffer), std::abs(value),
                    std::chars_format::scientific);
  const std::string_view scientific(buffer, result.ptr - buffer);
  const size_t e = scientific.find('e');
  std::string digits;
  for (const char c : scientific.substr(0, e)) {
    if (c != '.') digits += c;
  }
  const int digit_count = static_cast<int>(digits.size());
  // The position of the decimal point relative to the first digit.
  const int point =
      static_cast<int>(ParseInt(scientific.substr(e + 1)).value_or(0)) + 1;
  if (point <= -4 || point > 16) {
    out += digits[0];
    if (digit_count > 1) out += "." + digits.substr(1);
    out += std::format("e{}{:02}", point > 0 ? '+' : '-',
                       std::abs(point - 1));
  } else if (point <= 0) {
    out += "0." + std::string(-point, '0') + digits;
  } else if (point >= digit_count) {
    out += digits + std::string(point - digit_count, '0') + ".0";
  } else {
    out += digits.substr(0, point) + "." + digits.substr(point);
  }
  return out;
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
