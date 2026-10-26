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

// Returns `bytes` decoded like Python's bytes.decode('utf-8', 'replace'):
// each invalid sequence (the longest start of a valid sequence, or one byte)
// becomes U+FFFD.
std::string DecodeUtf8Replace(std::string_view bytes);

// Appends `code_point` to `out`, encoded as UTF-8.
void AppendUtf8(uint32_t code_point, std::string* out);

// Returns whether Python's str.isprintable() is true for `code_point`: false
// for control and format characters, separators other than the space,
// surrogates and private use characters. Unassigned code points, which
// Python also counts as not printable, are taken as printable.
bool IsPrintable(uint32_t code_point);

// Returns whether Python's str.isspace() is true for `code_point`: the
// characters str.split() and str.strip() treat as whitespace.
bool IsSpace(uint32_t code_point);

// Returns the number of code points in UTF-8 `text` (Python: len()).
size_t CodePointCount(std::string_view text);

// Returns the size in bytes of the first `count` code points of UTF-8
// `text` (Python: len(text[:count].encode())).
size_t CodePointPrefixSize(std::string_view text, size_t count);

}  // namespace cc::util

#endif  // CC_UTIL_UNICODE_H_
