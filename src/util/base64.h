// Base64 with the rules of Python's base64 module (libraries.md, "Small
// Python modules").

#ifndef CC_UTIL_BASE64_H_
#define CC_UTIL_BASE64_H_

#include <optional>
#include <string>
#include <string_view>

namespace cc::util {

// Returns `data` in Base64 with padding (Python: base64.b64encode(data)).
std::string Base64Encode(std::string_view data);

// Returns the bytes of the Base64 `text` (Python: base64.b64decode(text)).
// Characters outside the alphabet are dropped, and what follows the padding
// is ignored. nullopt if the text is cut off (Python: binascii.Error).
std::optional<std::string> Base64Decode(std::string_view text);

}  // namespace cc::util

#endif  // CC_UTIL_BASE64_H_
