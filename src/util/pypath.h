// Path string functions with the results of Python's os.path (posixpath)
// (libraries.md, "os.path functions").

#ifndef CC_UTIL_PYPATH_H_
#define CC_UTIL_PYPATH_H_

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// os.path.realpath(path): `path` made absolute with every symbolic link
// resolved. Parts that do not exist are kept as they are.
std::string Realpath(std::string_view path);

// os.path.splitext(path): the path without its extension, and the extension
// with its dot ("" if there is none).
std::pair<std::string, std::string> Splitext(std::string_view path);

// os.path.commonpath(paths): the longest common path of `paths`, which must
// not be empty; nullopt if absolute and relative paths are mixed (Python
// raises ValueError).
std::optional<std::string> Commonpath(const std::vector<std::string>& paths);

// os.path.expandvars(path): "$NAME" and "${NAME}" replaced by the
// environment variable NAME, if it is set.
std::string Expandvars(std::string_view path);

// os.path.exists(path), isfile(path) and isdir(path): symbolic links are
// followed, and errors mean false. IsLink() does not follow the last link
// (os.path.islink(path)).
bool Exists(const std::string& path);
bool IsFile(const std::string& path);
bool IsDir(const std::string& path);
bool IsLink(const std::string& path);

}  // namespace cc::util::pypath

#endif  // CC_UTIL_PYPATH_H_
