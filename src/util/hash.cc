#include "util/hash.h"

#include <openssl/evp.h>

#include <format>
#include <string>
#include <string_view>

namespace cc::util {

std::string Md5Hex(std::string_view data) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  EVP_Digest(data.data(), data.size(), digest, &size, EVP_md5(), nullptr);
  std::string hex;
  for (unsigned int i = 0; i < size; ++i) {
    hex += std::format("{:02x}", digest[i]);
  }
  return hex;
}

}  // namespace cc::util
