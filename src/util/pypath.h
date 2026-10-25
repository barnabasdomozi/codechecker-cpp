// Path string functions with the results of Python's os.path (posixpath)
// (libraries.md, "os.path functions").

#ifndef CC_UTIL_PYPATH_H_
#define CC_UTIL_PYPATH_H_

#include <initializer_list>
#include <string>
#include <string_view>

namespace cc::util::pypath {

// os.path.dirname(path).
std::string Dirname(std::string_view path);

// os.path.join(*parts): an absolute part discards the parts before it.
std::string Join(std::initializer_list<std::string_view> parts);

}  // namespace cc::util::pypath

#endif  // CC_UTIL_PYPATH_H_
