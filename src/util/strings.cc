#include "util/strings.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "util/result.h"
#include "util/unicode.h"
#include "util/xml.h"

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

// Returns the value of the hex digit `c`; nullopt if it is not one.
std::optional<int> HexDigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return std::nullopt;
}

// The characters of the numeric character references 0x80-0x9F, which name
// the Windows-1252 characters (Python: html._invalid_charrefs).
constexpr uint32_t kWindows1252[] = {
    0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
    0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

// The names Python's html.unescape() also accepts without ';' that are not
// HTML 4 names, with their characters.
constexpr std::pair<std::string_view, uint32_t> kUppercaseLegacyNames[] = {
    {"AMP", '&'}, {"COPY", 0xA9}, {"GT", '>'},
    {"LT", '<'},  {"QUOT", '"'},  {"REG", 0xAE}};

// Returns the text of the numeric character reference with the code
// `number`; nullopt stands for a number too big to parse, which becomes
// U+FFFD like other numbers out of range (Python: html._replace_charref()).
std::string NumericCharacter(std::optional<uint64_t> number) {
  std::string text;
  if (!number || *number > 0x10FFFF || (*number >= 0xD800 &&
                                         *number <= 0xDFFF)) {
    AppendUtf8(0xFFFD, &text);
    return text;
  }
  const uint32_t code = static_cast<uint32_t>(*number);
  if (code == 0) {
    AppendUtf8(0xFFFD, &text);
  } else if (code == '\r') {
    text = "\r";
  } else if (code >= 0x80 && code <= 0x9F) {
    AppendUtf8(kWindows1252[code - 0x80], &text);
  } else {
    // Python: html._invalid_codepoints, which are dropped.
    const bool invalid = (code >= 0x1 && code <= 0x8) || code == 0xB ||
                         (code >= 0xE && code <= 0x1F) || code == 0x7F ||
                         (code >= 0xFDD0 && code <= 0xFDEF) ||
                         (code & 0xFFFE) == 0xFFFE;
    if (!invalid) AppendUtf8(code, &text);
  }
  return text;
}

// Returns the character, in UTF-8, of the character reference name `name`;
// nullopt if it names none. With `legacy`, only the names that may be
// written without ';' count: those of the Latin-1 characters and a few
// others.
std::optional<std::string> NamedCharacter(std::string_view name,
                                          bool legacy) {
  std::optional<uint32_t> code = HtmlEntityCodePoint(name);
  if (legacy && code && *code > 0xFF) code = std::nullopt;
  for (const auto& [legacy_name, value] : kUppercaseLegacyNames) {
    if (name == legacy_name) code = value;
  }
  if (!code) return std::nullopt;
  std::string text;
  AppendUtf8(*code, &text);
  return text;
}

// Returns the text of the character reference `reference`, the part after
// '&' that matches Python's html._charref (Python: _replace_charref()).
std::string ReplaceCharacterReference(std::string_view reference) {
  if (reference.starts_with('#')) {
    const bool hex = reference.size() > 1 &&
                     (reference[1] == 'x' || reference[1] == 'X');
    std::string_view digits = reference.substr(hex ? 2 : 1);
    if (digits.ends_with(';')) digits.remove_suffix(1);
    uint64_t number = 0;
    const std::from_chars_result result = std::from_chars(
        digits.data(), digits.data() + digits.size(), number, hex ? 16 : 10);
    return NumericCharacter(result.ec == std::errc()
                                ? std::optional<uint64_t>(number)
                                : std::nullopt);
  }
  if (reference.ends_with(';')) {
    if (std::optional<std::string> character =
            NamedCharacter(reference.substr(0, reference.size() - 1),
                           /*legacy=*/false)) {
      return *character;
    }
  } else if (std::optional<std::string> character =
                 NamedCharacter(reference, /*legacy=*/true)) {
    return *character;
  }
  // The longest name at the start, of those that need no ';'.
  for (size_t size = reference.size() - 1; size > 1; --size) {
    if (std::optional<std::string> character =
            NamedCharacter(reference.substr(0, size), /*legacy=*/true)) {
      return *character + std::string(reference.substr(size));
    }
  }
  return "&" + std::string(reference);
}

// Returns the size of the character reference that starts at `text`, the
// part after '&' (Python: the regular expression html._charref); 0 if none
// starts there.
size_t CharacterReferenceSize(std::string_view text) {
  auto is_digit = [](char c, bool hex) {
    return (c >= '0' && c <= '9') ||
           (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
  };
  size_t size = 0;
  if (text.starts_with('#')) {
    const bool hex = text.size() > 1 && (text[1] == 'x' || text[1] == 'X');
    const size_t start = hex ? 2 : 1;
    size = start;
    while (size < text.size() && is_digit(text[size], hex)) ++size;
    if (size == start) return 0;
  } else {
    constexpr std::string_view kNotInName("\t\n\f <&#;");
    while (size < text.size() && size < 32 &&
           kNotInName.find(text[size]) == std::string_view::npos) {
      ++size;
    }
    if (size == 0) return 0;
  }
  if (size < text.size() && text[size] == ';') ++size;
  return size;
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

Error KeyError(std::string_view key) {
  return Error{.message = Repr(key)};
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

std::string_view LeftStrip(std::string_view text,
                           std::string_view characters) {
  const size_t start = text.find_first_not_of(characters);
  return start == std::string_view::npos ? std::string_view()
                                         : text.substr(start);
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

std::vector<std::string_view> StrSplitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t boundary = pos;
    const uint32_t c = NextCodePoint(text, &pos).value_or(0);
    const bool line_break = c == '\n' || c == '\r' || c == 0x0B ||
                            c == 0x0C || c == 0x1C || c == 0x1D ||
                            c == 0x1E || c == 0x85 || c == 0x2028 ||
                            c == 0x2029;
    if (!line_break) continue;
    lines.push_back(text.substr(start, boundary - start));
    if (c == '\r' && pos < text.size() && text[pos] == '\n') ++pos;
    start = pos;
  }
  if (start < text.size()) lines.push_back(text.substr(start));
  return lines;
}

std::vector<std::string> SplitWhitespace(std::string_view text) {
  std::vector<std::string> words;
  std::string word;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t start = pos;
    const std::optional<uint32_t> c = NextCodePoint(text, &pos);
    if (c && IsSpace(*c)) {
      if (!word.empty()) words.push_back(std::move(word));
      word.clear();
    } else {
      word.append(text.substr(start, pos - start));
    }
  }
  if (!word.empty()) words.push_back(std::move(word));
  return words;
}

std::vector<std::string> Split(std::string_view text,
                               std::string_view separator, int max_splits) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (max_splits < 0 || static_cast<int>(parts.size()) < max_splits) {
    const size_t found = text.find(separator, start);
    if (found == std::string_view::npos) break;
    parts.emplace_back(text.substr(start, found - start));
    start = found + separator.size();
  }
  parts.emplace_back(text.substr(start));
  return parts;
}

std::string Replace(std::string_view text, std::string_view from,
                    std::string_view to) {
  return Join(Split(text, from), to);
}

std::string AsciiLower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return result;
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

std::string HtmlEscape(std::string_view text) {
  std::string result;
  for (const char c : text) {
    switch (c) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      case '"':
        result += "&quot;";
        break;
      case '\'':
        result += "&#x27;";
        break;
      default:
        result += c;
    }
  }
  return result;
}

std::string HtmlUnescape(std::string_view text) {
  std::string result;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t amp = text.find('&', pos);
    if (amp == std::string_view::npos) break;
    result += text.substr(pos, amp - pos);
    const std::string_view rest = text.substr(amp + 1);
    const size_t size = CharacterReferenceSize(rest);
    if (size == 0) {
      result += '&';
    } else {
      result += ReplaceCharacterReference(rest.substr(0, size));
    }
    pos = amp + 1 + size;
  }
  result += text.substr(std::min(pos, text.size()));
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

std::string BracketIpv6Host(std::string_view host) {
  if (host.find(':') == std::string_view::npos) return std::string(host);
  return std::format("[{}]", host);
}

std::string PercentDecode(std::string_view text) {
  std::string decoded;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const std::optional<int> high = HexDigitValue(text[i + 1]);
      const std::optional<int> low = HexDigitValue(text[i + 2]);
      if (high && low) {
        decoded += static_cast<char>(*high * 16 + *low);
        i += 2;
        continue;
      }
    }
    decoded += text[i];
  }
  return decoded;
}

}  // namespace cc::util
