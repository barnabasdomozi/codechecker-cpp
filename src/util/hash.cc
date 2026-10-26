#include "util/hash.h"

#include <openssl/evp.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace cc::util {
namespace {

// A hashlib constructor and the name OpenSSL knows the algorithm by.
struct DigestName {
  const char* python;
  const char* openssl;
};

// The constructors of hashlib.algorithms_guaranteed with a fixed digest
// size (the shake_* ones need a length).
constexpr DigestName kDigestNames[] = {
    {"md5", "MD5"},           {"sha1", "SHA1"},
    {"sha224", "SHA224"},     {"sha256", "SHA256"},
    {"sha384", "SHA384"},     {"sha512", "SHA512"},
    {"sha3_224", "SHA3-224"}, {"sha3_256", "SHA3-256"},
    {"sha3_384", "SHA3-384"}, {"sha3_512", "SHA3-512"},
    {"blake2b", "BLAKE2B-512"}, {"blake2s", "BLAKE2S-256"},
};

// Returns the digest of `data` with `md` as lowercase hex digits.
std::string Hex(const EVP_MD* md, std::string_view data) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  EVP_Digest(data.data(), data.size(), digest, &size, md, nullptr);
  std::string hex;
  for (unsigned int i = 0; i < size; ++i) {
    hex += std::format("{:02x}", digest[i]);
  }
  return hex;
}

}  // namespace

std::string Md5Hex(std::string_view data) { return Hex(EVP_md5(), data); }

std::optional<std::string> HexDigest(std::string_view name,
                                     std::string_view data) {
  for (const DigestName& digest : kDigestNames) {
    if (name != digest.python) continue;
    const EVP_MD* md = EVP_get_digestbyname(digest.openssl);
    if (md == nullptr) return std::nullopt;
    return Hex(md, data);
  }
  return std::nullopt;
}

}  // namespace cc::util
