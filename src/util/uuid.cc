#include "util/uuid.h"

#include <openssl/rand.h>
#include <stddef.h>

#include <format>
#include <optional>
#include <string>
#include <vector>

namespace cc::util {

std::string Uuid4() {
  unsigned char bytes[16] = {};
  RAND_bytes(bytes, sizeof(bytes));
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);  // Version.
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);  // Variant.
  std::string text;
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) text += '-';
    text += std::format("{:02x}", bytes[i]);
  }
  return text;
}

std::optional<std::string> RandomHexToken(int length) {
  if (length <= 0) return std::string();
  std::vector<unsigned char> bytes(static_cast<size_t>((length + 1) / 2));
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    return std::nullopt;
  }
  std::string text;
  for (const unsigned char byte : bytes) text += std::format("{:02x}", byte);
  text.resize(static_cast<size_t>(length));
  return text;
}

}  // namespace cc::util
