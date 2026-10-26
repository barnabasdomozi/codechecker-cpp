// Message digests (Python: hashlib), computed by OpenSSL (libraries.md,
// "Hashes, UUIDs, secure random bytes, TLS").

#ifndef CC_UTIL_HASH_H_
#define CC_UTIL_HASH_H_

#include <optional>
#include <string>
#include <string_view>

namespace cc::util {

// Returns the MD5 digest of `data` as 32 lowercase hex digits (Python:
// hashlib.md5(data).hexdigest()).
std::string Md5Hex(std::string_view data);

// Returns the digest of `data` with the hashlib algorithm `name` ("sha256",
// "sha3_512", "blake2b", ...) as lowercase hex digits (Python:
// getattr(hashlib, name)(data).hexdigest()); nullopt if hashlib has no such
// algorithm with a fixed digest size.
std::optional<std::string> HexDigest(std::string_view name,
                                     std::string_view data);

}  // namespace cc::util

#endif  // CC_UTIL_HASH_H_
