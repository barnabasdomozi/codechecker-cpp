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

// A folder that Walk() visits, and the files in it.
struct WalkedFolder {
  std::string path;
  // The entries that are not folders (symbolic links followed), as
  // os.path.join(path, <name>), in the order the system lists them.
  std::vector<std::string> files;
};

// Returns `path` and the folders under it in the order os.walk(path) visits
// them: a folder, then its subfolders, but not subfolders that are symbolic
// links. A folder that cannot be listed is left out, with what is under
// it, as os.walk() does.
std::vector<WalkedFolder> Walk(const std::string& path);

// Returns the files of every folder of Walk(path), in that order.
std::vector<std::string> WalkFiles(const std::string& path);

// Returns os.stat(path).st_mtime: the modification time of `path` in
// seconds since the epoch, symbolic links followed; nullopt if stat()
// fails.
std::optional<double> ModificationTime(const std::string& path);

// Returns the permission bits of `path` (Python: os.stat(path).st_mode &
// 0o7777), symbolic links followed; nullopt if stat() fails.
std::optional<int> FileMode(const std::string& path);

// Sets the permission bits of `path` (Python: os.chmod(path, mode)).
std::error_code SetFileMode(const std::string& path, int mode);

// Returns the home folder of the user (Python: os.path.expanduser("~")):
// HOME, else the home folder in the password database.
std::string HomeDirectory();

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
