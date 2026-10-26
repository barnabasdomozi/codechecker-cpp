// File system operations of Python's os and shutil modules that the
// standard library does not do the same way.

#ifndef CC_PLATFORM_FILESYSTEM_H_
#define CC_PLATFORM_FILESYSTEM_H_

#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace cc::platform {

// Returns the names of the entries in the folder `path`, in the order the
// system lists them (Python: os.listdir(path)). Empty on errors.
std::vector<std::string> ListDirectory(const std::string& path);

// Returns the files under the folder `path`, in the order os.walk(path)
// visits them: the entries of a folder that are not folders (symbolic links
// followed) as os.path.join(<folder>, <name>), then its subfolders, but not
// subfolders that are symbolic links. Errors are ignored, as os.walk() does.
std::vector<std::string> WalkFiles(const std::string& path);

// Creates the folder `path` and its missing parents (Python:
// os.makedirs(path)). It is an error if `path` exists.
std::error_code MakeDirs(const std::string& path);

// Creates the folder `path` and its missing parents; an existing folder is
// not an error (Python: os.makedirs(path, exist_ok=True)).
std::error_code EnsureDirs(const std::string& path);

// Copies the file `source` to `destination`, replacing it (Python:
// shutil.copyfile(source, destination)).
std::error_code CopyFile(const std::string& source,
                         const std::string& destination);

// Deletes `path` and everything in it (Python: shutil.rmtree(path)).
std::error_code RemoveTree(const std::string& path);

// Returns the folder for temporary files (Python: tempfile.gettempdir()):
// the first usable of TMPDIR, TEMP, TMP, /tmp, /var/tmp, /usr/tmp and the
// current folder.
std::string TempDir();

// Creates a new, empty folder in TempDir() and returns its path (Python:
// tempfile.mkdtemp()); nullopt if that fails.
std::optional<std::string> MakeTempDir();

// Creates a new, empty file named "tmp<random><suffix>" in the folder `dir`
// and returns its path (Python: tempfile.mkstemp(suffix, dir=dir)); nullopt
// if that fails.
std::optional<std::string> MakeTempFile(const std::string& dir,
                                        const std::string& suffix);

}  // namespace cc::platform

#endif  // CC_PLATFORM_FILESYSTEM_H_
