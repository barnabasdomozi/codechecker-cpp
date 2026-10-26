#include "util/base64.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

namespace cc::util {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string Base64Encode(std::string_view data) {
  std::string text;
  text.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const size_t count = data.size() - i < 3 ? data.size() - i : 3;
    uint32_t group = 0;
    for (size_t j = 0; j < 3; ++j) {
      const uint32_t byte =
          j < count ? static_cast<unsigned char>(data[i + j]) : 0;
      group = (group << 8) | byte;
    }
    for (size_t j = 0; j < 4; ++j) {
      text += j <= count ? kAlphabet[(group >> (18 - 6 * j)) & 0x3F] : '=';
    }
  }
  return text;
}

// Like binascii.a2b_base64() of Python 3.13 in its default, non-strict mode:
// '=' counts only as padding after the last data character.
std::optional<std::string> Base64Decode(std::string_view text) {
  std::string data;
  uint32_t group = 0;
  int group_size = 0;  // Characters of the current group of four.
  int trailing_pads = 0;
  for (const char c : text) {
    if (c == '=') {
      ++trailing_pads;
      continue;
    }
    const size_t value = kAlphabet.find(c);
    if (value == std::string_view::npos) continue;
    trailing_pads = 0;
    group = (group << 6) | static_cast<uint32_t>(value);
    if (++group_size == 4) {
      data += static_cast<char>((group >> 16) & 0xFF);
      data += static_cast<char>((group >> 8) & 0xFF);
      data += static_cast<char>(group & 0xFF);
      group = 0;
      group_size = 0;
    }
  }
  // A group of two or three characters holds one or two bytes and needs
  // two or one '='.
  if (group_size == 0) return data;
  if (group_size == 1 || group_size + trailing_pads < 4) return std::nullopt;
  group <<= 6 * (4 - group_size);
  data += static_cast<char>((group >> 16) & 0xFF);
  if (group_size == 3) data += static_cast<char>((group >> 8) & 0xFF);
  return data;
}

}  // namespace cc::util
