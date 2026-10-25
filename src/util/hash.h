// Message digests (Python: hashlib), computed by OpenSSL (libraries.md,
// "Hashes, UUIDs, secure random bytes, TLS").

#ifndef CC_UTIL_HASH_H_
#define CC_UTIL_HASH_H_

#include <string>
#include <string_view>

namespace cc::util {

// Returns the MD5 digest of `data` as 32 lowercase hex digits (Python:
// hashlib.md5(data).hexdigest()).
std::string Md5Hex(std::string_view data);

}  // namespace cc::util

#endif  // CC_UTIL_HASH_H_
