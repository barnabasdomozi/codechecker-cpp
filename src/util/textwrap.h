// Port of Python's textwrap.wrap(), used by argparse help texts and table
// cells.

#ifndef CC_UTIL_TEXTWRAP_H_
#define CC_UTIL_TEXTWRAP_H_

#include <string>
#include <string_view>
#include <vector>

namespace cc::util {

// Wraps `text` into lines of at most `width` code points, like Python's
// textwrap.wrap(text, width, initial_indent=indent,
// subsequent_indent=indent) with the other settings at their defaults:
// tabs expanded, whitespace turned into spaces and dropped at line ends,
// lines broken at spaces and inside hyphenated words ("goof-ball"), and
// words longer than a line broken, after a hyphen if possible. `width` must
// be positive.
std::vector<std::string> Wrap(std::string_view text, int width,
                              std::string_view indent = "");

}  // namespace cc::util

#endif  // CC_UTIL_TEXTWRAP_H_
