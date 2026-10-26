// Port of Python's fnmatch module (3.13): shell-style wildcards turned into
// regular expressions (libraries.md, "Small Python modules"). `*` also
// matches "/", and there is no way to quote a wildcard.

#ifndef CC_UTIL_FNMATCH_H_
#define CC_UTIL_FNMATCH_H_

#include <string>
#include <string_view>

namespace cc::util {

// Returns the regular expression that matches what `pattern` matches, in
// Python's syntax and ending in "\Z" (Python: fnmatch.translate()).
std::string FnmatchTranslate(std::string_view pattern);

// Returns whether `name` matches the shell pattern `pattern`, case-sensitive
// (Python: fnmatch.fnmatch() on POSIX).
bool FnMatch(std::string_view name, std::string_view pattern);

}  // namespace cc::util

#endif  // CC_UTIL_FNMATCH_H_
