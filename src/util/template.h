// Text templates with the substitution rules of Python's string.Template
// (libraries.md, "Small Python modules").

#ifndef CC_UTIL_TEMPLATE_H_
#define CC_UTIL_TEMPLATE_H_

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "util/result.h"

namespace cc::util {

// Returns `text` with each "$name" and "${name}" replaced by the value of
// `name` in `values`, and each "$$" by "$" (Python:
// string.Template(text).substitute(values)). A name is an ASCII letter or
// "_", then ASCII letters, digits and "_". Fails with the text of Python's
// exception for a name that is not in `values` and for a "$" that starts
// none of these.
Result<std::string> SubstituteTemplate(
    std::string_view text,
    const std::map<std::string, std::string, std::less<>>& values);

}  // namespace cc::util

#endif  // CC_UTIL_TEMPLATE_H_
