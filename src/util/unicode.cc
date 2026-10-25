#include "util/unicode.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

namespace cc::util {
namespace {

bool IsContinuation(uint32_t byte) { return (byte & 0xC0) == 0x80; }

}  // namespace

std::optional<uint32_t> NextCodePoint(std::string_view text, size_t* pos) {
  const uint32_t first = static_cast<unsigned char>(text[*pos]);
  int length = 0;
  uint32_t min_second = 0x80;
  uint32_t max_second = 0xBF;
  if (first < 0x80) {
    ++*pos;
    return first;
  } else if (first >= 0xC2 && first <= 0xDF) {
    length = 2;
  } else if (first >= 0xE0 && first <= 0xEF) {
    length = 3;
    if (first == 0xE0) min_second = 0xA0;  // No overlong forms.
    if (first == 0xED) max_second = 0x9F;  // No surrogates.
  } else if (first >= 0xF0 && first <= 0xF4) {
    length = 4;
    if (first == 0xF0) min_second = 0x90;  // No overlong forms.
    if (first == 0xF4) max_second = 0x8F;  // Nothing above U+10FFFF.
  } else {
    ++*pos;
    return std::nullopt;
  }
  if (text.size() - *pos < static_cast<size_t>(length)) {
    ++*pos;
    return std::nullopt;
  }
  const uint32_t second = static_cast<unsigned char>(text[*pos + 1]);
  if (second < min_second || second > max_second) {
    ++*pos;
    return std::nullopt;
  }
  uint32_t code_point = first & (0x7F >> length);
  for (int i = 1; i < length; ++i) {
    const uint32_t byte = static_cast<unsigned char>(text[*pos + i]);
    if (!IsContinuation(byte)) {
      ++*pos;
      return std::nullopt;
    }
    code_point = (code_point << 6) | (byte & 0x3F);
  }
  *pos += length;
  return code_point;
}

std::string DecodeUtf8Ignore(std::string_view bytes) {
  std::string text;
  text.reserve(bytes.size());
  size_t pos = 0;
  while (pos < bytes.size()) {
    const size_t start = pos;
    if (NextCodePoint(bytes, &pos)) {
      text.append(bytes.substr(start, pos - start));
    }
  }
  return text;
}

namespace {

// Returns the length of the invalid sequence at byte `pos` of `text`: the
// bytes that start a valid sequence but do not finish it, or one byte (the
// "maximal subpart" that Python's decoder replaces as one).
size_t InvalidSequenceSize(std::string_view text, size_t pos) {
  const uint32_t first = static_cast<unsigned char>(text[pos]);
  size_t length = 0;
  uint32_t min_second = 0x80;
  uint32_t max_second = 0xBF;
  if (first >= 0xC2 && first <= 0xDF) {
    length = 2;
  } else if (first >= 0xE0 && first <= 0xEF) {
    length = 3;
    if (first == 0xE0) min_second = 0xA0;
    if (first == 0xED) max_second = 0x9F;
  } else if (first >= 0xF0 && first <= 0xF4) {
    length = 4;
    if (first == 0xF0) min_second = 0x90;
    if (first == 0xF4) max_second = 0x8F;
  } else {
    return 1;
  }
  size_t size = 1;
  while (size < length && pos + size < text.size()) {
    const uint32_t byte = static_cast<unsigned char>(text[pos + size]);
    const bool valid = size == 1 ? byte >= min_second && byte <= max_second
                                 : IsContinuation(byte);
    if (!valid) break;
    ++size;
  }
  return size;
}

}  // namespace

std::string DecodeUtf8Replace(std::string_view bytes) {
  std::string text;
  text.reserve(bytes.size());
  size_t pos = 0;
  while (pos < bytes.size()) {
    const size_t start = pos;
    if (NextCodePoint(bytes, &pos)) {
      text.append(bytes.substr(start, pos - start));
    } else {
      text += "\xEF\xBF\xBD";  // U+FFFD.
      pos = start + InvalidSequenceSize(bytes, start);
    }
  }
  return text;
}

bool IsSpace(uint32_t code_point) {
  // The code points of the Unicode database of Python 3.13 for which
  // str.isspace() is true.
  return (code_point >= 0x09 && code_point <= 0x0D) ||
         (code_point >= 0x1C && code_point <= 0x20) || code_point == 0x85 ||
         code_point == 0xA0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200A) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202F || code_point == 0x205F || code_point == 0x3000;
}

size_t CodePointCount(std::string_view text) {
  size_t count = 0;
  for (size_t pos = 0; pos < text.size(); ++count) NextCodePoint(text, &pos);
  return count;
}

size_t CodePointPrefixSize(std::string_view text, size_t count) {
  size_t pos = 0;
  for (size_t i = 0; i < count && pos < text.size(); ++i) {
    NextCodePoint(text, &pos);
  }
  return pos;
}

}  // namespace cc::util
