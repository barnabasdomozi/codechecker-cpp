// Port of the parts of Python's shlex module that the commands use
// (libraries.md, "Small Python modules"): POSIX-mode splitting and quoting of
// command lines.

#ifndef CC_UTIL_SHLEX_H_
#define CC_UTIL_SHLEX_H_

#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::util {

// Splits `text` into arguments like Python's shlex.split(text): POSIX rules,
// no comments. Fails with Python's ValueError message ("No closing
// quotation", "No escaped character").
Result<std::vector<std::string>> ShlexSplit(std::string_view text);

// Returns `text` quoted for a POSIX shell (Python: shlex.quote(text)).
std::string ShlexQuote(std::string_view text);

// Returns the arguments quoted and joined with spaces (Python:
// shlex.join(args)).
std::string ShlexJoin(const std::vector<std::string>& args);

}  // namespace cc::util

#endif  // CC_UTIL_SHLEX_H_
