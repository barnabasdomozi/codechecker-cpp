// Random UUIDs (Python: uuid.uuid4()), from OpenSSL's secure random bytes
// (libraries.md, "Hashes, UUIDs, secure random bytes, TLS").

#ifndef CC_UTIL_UUID_H_
#define CC_UTIL_UUID_H_

#include <string>

namespace cc::util {

// Returns a new random UUID as Python's str(uuid.uuid4()) writes it, for
// example "16fd2706-8baf-433b-82eb-8c7fada847da".
std::string Uuid4();

}  // namespace cc::util

#endif  // CC_UTIL_UUID_H_
