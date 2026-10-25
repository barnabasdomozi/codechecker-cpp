// Path string functions with the results of Python's os.path (posixpath)
// (libraries.md, "os.path functions").

#ifndef CC_UTIL_PYPATH_H_
#define CC_UTIL_PYPATH_H_

#include <initializer_list>
#include <string>
#include <string_view>

namespace cc::util::pypath {

// os.path.basename(path).
std::string Basename(std::string_view path);

// os.path.dirname(path).
std::string Dirname(std::string_view path);

// os.path.join(*parts): an absolute part discards the parts before it.
std::string Join(std::initializer_list<std::string_view> parts);

// os.path.normpath(path): removes ".", "..", repeated and trailing slashes
// without looking at the disk. POSIX keeps exactly two leading slashes.
std::string Normpath(std::string_view path);

// os.path.abspath(path): normpath(join(os.getcwd(), path)).
std::string Abspath(std::string_view path);

// os.path.exists(path), isfile(path) and isdir(path): symbolic links are
// followed, and errors mean false.
bool Exists(const std::string& path);
bool IsFile(const std::string& path);
bool IsDir(const std::string& path);

}  // namespace cc::util::pypath

#endif  // CC_UTIL_PYPATH_H_
