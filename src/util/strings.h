// String functions with the results of Python's str built-ins.

#ifndef CC_UTIL_STRINGS_H_
#define CC_UTIL_STRINGS_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc::util {

// Returns `text` as Python's repr() of a str shows it, quotes included, for
// example 'foo' or "it's".
std::string Repr(std::string_view text);

// Parses `text` like Python's int(): optional surrounding whitespace and
// sign, then decimal digits. Returns nullopt where int() raises ValueError.
// Underscores between digits are not accepted.
std::optional<int64_t> ParseInt(std::string_view text);

// Returns `text` without leading and trailing ASCII whitespace.
std::string_view StripAsciiWhitespace(std::string_view text);

// Returns `text` without leading and trailing whitespace (Python:
// str.strip()).
std::string_view Strip(std::string_view text);

// Returns `text` without trailing whitespace (Python: str.rstrip()).
std::string_view RightStrip(std::string_view text);

// Returns `text` with all whitespace removed (Python: ''.join(text.split())).
std::string RemoveWhitespace(std::string_view text);

// Returns `text` with "\r\n" and a lone "\r" turned into "\n", as Python's
// text files read it (universal newlines).
std::string TranslateNewlines(std::string_view text);

// Returns the lines of `text`, each with its line terminator: "\n", "\r\n"
// or "\r" (Python: the lines of a text file opened with newline=''). The
// lines point into `text`.
std::vector<std::string_view> SplitLines(std::string_view text);

// Returns `value` as Python's repr(float) writes it, for example "1.0",
// "1e-05", "inf" and "nan".
std::string FloatRepr(double value);

// Returns `text` with tabs replaced by spaces up to the next multiple of 8
// columns, counted in code points (Python: str.expandtabs()).
std::string ExpandTabs(std::string_view text);

// Returns `text` padded with spaces to `width` code points (Python:
// str.ljust()).
std::string LeftJustify(std::string_view text, int width);

// Returns `parts` joined with `separator` between them (Python:
// separator.join(parts)).
std::string Join(const std::vector<std::string>& parts,
                 std::string_view separator);

}  // namespace cc::util

#endif  // CC_UTIL_STRINGS_H_
