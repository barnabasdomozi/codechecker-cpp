// String functions with the results of Python's str built-ins.

#ifndef CC_UTIL_STRINGS_H_
#define CC_UTIL_STRINGS_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::util {

// Returns `text` as Python's repr() of a str shows it, quotes included, for
// example 'foo' or "it's".
std::string Repr(std::string_view text);

// Returns the error of a Python KeyError for `key`, with the message that
// str() gives it: the repr of `key`.
Error KeyError(std::string_view key);

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

// Returns `text` without its leading characters that are in the ASCII
// `characters` (Python: str.lstrip(characters)).
std::string_view LeftStrip(std::string_view text, std::string_view characters);

// Returns `text` with all whitespace removed (Python: ''.join(text.split())).
std::string RemoveWhitespace(std::string_view text);

// Returns `text` with "\r\n" and a lone "\r" turned into "\n", as Python's
// text files read it (universal newlines).
std::string TranslateNewlines(std::string_view text);

// Returns the lines of `text`, each with its line terminator: "\n", "\r\n"
// or "\r" (Python: the lines of a text file opened with newline=''). The
// lines point into `text`.
std::vector<std::string_view> SplitLines(std::string_view text);

// Returns the lines of `text` without their line terminators, split at
// every line boundary Python's str.splitlines() knows: "\n", "\r", "\r\n",
// "\v", "\f", "\x1c", "\x1d", "\x1e", U+0085, U+2028 and U+2029. The
// lines point into `text`.
std::vector<std::string_view> StrSplitLines(std::string_view text);

// Returns the words of `text`, separated by runs of whitespace (Python:
// str.split()).
std::vector<std::string> SplitWhitespace(std::string_view text);

// Returns the parts of `text` between occurrences of `separator`, which must
// not be empty, split at the first `max_splits` occurrences only if it is not
// negative (Python: str.split(separator, max_splits)).
std::vector<std::string> Split(std::string_view text,
                               std::string_view separator,
                               int max_splits = -1);

// Returns `text` with every `from` replaced by `to`; `from` must not be
// empty (Python: str.replace(from, to)).
std::string Replace(std::string_view text, std::string_view from,
                    std::string_view to);

// Returns `text` with the ASCII letters lower-cased (Python: str.lower() on
// ASCII text).
std::string AsciiLower(std::string_view text);

// Returns `value` as Python's repr(float) writes it, for example "1.0",
// "1e-05", "inf" and "nan".
std::string FloatRepr(double value);

// Returns `text` with tabs replaced by spaces up to the next multiple of 8
// columns, counted in code points (Python: str.expandtabs()).
std::string ExpandTabs(std::string_view text);

// Returns `text` padded with spaces to `width` code points (Python:
// str.ljust()).
std::string LeftJustify(std::string_view text, int width);

// Returns `text` with the characters & < > " ' replaced by HTML character
// references (Python: html.escape(text)).
std::string HtmlEscape(std::string_view text);

// Returns `text` with its HTML character references replaced by the
// characters they name (Python: html.unescape()). Numeric references follow
// Python's rules; names are those of HTML 4 (libxml2's table), the ones
// Python also accepts without ';' included. Names that only HTML 5 has are
// left as they are.
std::string HtmlUnescape(std::string_view text);

// Returns `text` with its %XX escapes decoded (Python:
// urllib.parse.unquote_to_bytes()).
std::string PercentDecode(std::string_view text);

// Returns `host` in brackets if it is an IPv6 address, which has a ':', as
// URLs and the server's log lines write a host: "[::1]", "localhost".
std::string BracketIpv6Host(std::string_view host);

// Returns `parts` joined with `separator` between them (Python:
// separator.join(parts)).
std::string Join(const std::vector<std::string>& parts,
                 std::string_view separator);

}  // namespace cc::util

#endif  // CC_UTIL_STRINGS_H_
