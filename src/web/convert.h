// Port of codechecker_web/shared/convert.py: the Base64 texts of the API.
// Encoding needs nothing more than util::Base64Encode() (Python:
// convert.to_b64()), so only decoding is here.

#ifndef CC_WEB_CONVERT_H_
#define CC_WEB_CONVERT_H_

#include <optional>
#include <string>
#include <string_view>

namespace cc::web {

// Returns the text in the Base64 `text`, with invalid UTF-8 dropped
// (Python: convert.from_b64()); nullopt if it is not Base64.
std::optional<std::string> FromBase64(std::string_view text);

}  // namespace cc::web

#endif  // CC_WEB_CONVERT_H_
