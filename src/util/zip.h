// ZIP archives read and written with libzip (libraries.md, "ZIP archives:
// libzip"), like Python's zipfile does it. An archive never holds two
// entries with the same name: adding a name that exists replaces the entry.

#ifndef CC_UTIL_ZIP_H_
#define CC_UTIL_ZIP_H_

#include <stdint.h>
#include <zip.h>

#include <memory>
#include <string>
#include <vector>

#include "util/result.h"

namespace cc::util {

// An open archive. Changes are written by Close(); an archive destroyed
// without Close() is left as it was.
//
//   Result<ZipArchive> zip = ZipArchive::Open(path, ZipArchive::Mode::kAppend);
//   if (!zip.ok()) return zip.error();
//   zip.value().AddText("stdout", out);
//   Result<void> closed = zip.value().Close();
class ZipArchive {
 public:
  enum class Mode {
    kWrite,   // A new archive, replacing the file (Python: mode 'w').
    kAppend,  // Adds to the archive, which is created if missing ('a').
  };

  // Opens the archive at `path`. The error message is libzip's.
  static Result<ZipArchive> Open(const std::string& path, Mode mode);

  ZipArchive(ZipArchive&& other) noexcept;
  ZipArchive& operator=(ZipArchive&& other) noexcept;
  ~ZipArchive();

  // Returns whether the archive has an entry `name`.
  bool Contains(const std::string& name) const;

  // Adds the file at `source` as the deflated entry `name` (Python:
  // write(source, name, ZIP_DEFLATED)).
  Result<void> AddFile(const std::string& name, const std::string& source);

  // Adds `text` as the stored entry `name` (Python: writestr(name, text)).
  Result<void> AddText(const std::string& name, std::string text);

  // Writes the archive and closes it.
  Result<void> Close();

 private:
  explicit ZipArchive(zip_t* archive) : archive_(archive) {}

  zip_t* archive_;
  // The texts of AddText(), which libzip reads only in Close().
  std::vector<std::unique_ptr<std::string>> texts_;
};

// An entry of an archive (Python: a ZipInfo).
struct ZipEntry {
  std::string name;
  int64_t size = 0;  // Uncompressed.
};

// Returns the entries of the archive at `path`, in their order (Python:
// ZipFile(path).infolist()). The error message is libzip's.
Result<std::vector<ZipEntry>> ListZipEntries(const std::string& path);

// Writes the entries of the archive at `path` into the folder `directory`,
// creating the folders they need (Python: ZipFile(path).extractall(
// directory)). As in Python, a name loses its leading slashes and its "."
// and ".." parts, so nothing is written outside `directory`, and a name
// that ends with "/" is a folder. The error message is libzip's, or the
// one of the file that cannot be written.
Result<void> ExtractZip(const std::string& path, const std::string& directory);

}  // namespace cc::util

#endif  // CC_UTIL_ZIP_H_
