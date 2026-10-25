// UTF-8 helpers that give Python's str results on UTF-8 encoded
// std::string (libraries.md, "Unicode text handling").

#ifndef CC_UTIL_UNICODE_H_
#define CC_UTIL_UNICODE_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

namespace cc::util {

// Decodes the code point that starts at byte `*pos` of `text` and moves
// `*pos` past it. If the bytes there are not valid UTF-8 (by the rules of
// Python's strict decoder), returns nullopt and moves `*pos` past one byte.
// `*pos` must be less than text.size().
std::optional<uint32_t> NextCodePoint(std::string_view text, size_t* pos);

// Returns `bytes` decoded like Python's bytes.decode('utf-8', 'ignore'):
// invalid bytes are dropped.
std::string DecodeUtf8Ignore(std::string_view bytes);

// Returns the number of code points in UTF-8 `text` (Python: len()).
size_t CodePointCount(std::string_view text);

// Returns the size in bytes of the first `count` code points of UTF-8
// `text` (Python: len(text[:count].encode())).
size_t CodePointPrefixSize(std::string_view text, size_t count);

}  // namespace cc::util

#endif  // CC_UTIL_UNICODE_H_
