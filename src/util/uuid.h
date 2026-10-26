// Random identifiers from OpenSSL's secure random bytes (libraries.md,
// "Hashes, UUIDs, secure random bytes, TLS"): UUIDs (Python: uuid.uuid4())
// and hex tokens.

#ifndef CC_UTIL_UUID_H_
#define CC_UTIL_UUID_H_

#include <optional>
#include <string>

namespace cc::util {

// Returns a new random UUID as Python's str(uuid.uuid4()) writes it, for
// example "16fd2706-8baf-433b-82eb-8c7fada847da".
std::string Uuid4();

// Returns `length` random lowercase hex digits, for session tokens and task
// tokens (Python: codechecker_common.util.generate_random_token(length));
// "" if `length` is not positive. nullopt if OpenSSL cannot make secure
// random bytes.
std::optional<std::string> RandomHexToken(int length);

}  // namespace cc::util

#endif  // CC_UTIL_UUID_H_
