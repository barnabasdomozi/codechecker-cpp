#include "util/zip.h"

#include <stdint.h>
#include <zip.h>

#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "util/file.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::util {
namespace {

// Returns the error of `archive` with libzip's message.
Error ArchiveError(zip_t* archive) {
  return Error{.message = zip_strerror(archive)};
}

// Adds `source` as the entry `name` with the compression `method`.
Result<void> AddSource(zip_t* archive, const std::string& name,
                       zip_source_t* source, int32_t method) {
  if (source == nullptr) return Result<void>(ArchiveError(archive));
  const zip_int64_t index = zip_file_add(
      archive, name.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
  if (index < 0) {
    zip_source_free(source);
    return Result<void>(ArchiveError(archive));
  }
  if (zip_set_file_compression(archive, static_cast<zip_uint64_t>(index),
                               method, 0) != 0) {
    return Result<void>(ArchiveError(archive));
  }
  return Result<void>();
}

// Returns the error of libzip's `error_code`.
Error CodeError(int error_code) {
  zip_error_t error;
  zip_error_init_with_code(&error, error_code);
  std::string message = zip_error_strerror(&error);
  zip_error_fini(&error);
  return Error{.message = std::move(message)};
}

// Closes an archive opened for reading.
struct ReadArchiveCloser {
  void operator()(zip_t* archive) const { zip_discard(archive); }
};

// An archive opened for reading.
using ReadArchive = std::unique_ptr<zip_t, ReadArchiveCloser>;

// Opens the archive at `path` for reading.
Result<ReadArchive> OpenForReading(const std::string& path) {
  int error_code = 0;
  zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, &error_code);
  if (archive == nullptr) return Result<ReadArchive>(CodeError(error_code));
  return Result<ReadArchive>(ReadArchive(archive));
}

// Returns the entries of `archive`.
Result<std::vector<ZipEntry>> Entries(zip_t* archive) {
  std::vector<ZipEntry> entries;
  const zip_int64_t count = zip_get_num_entries(archive, 0);
  for (zip_int64_t i = 0; i < count; ++i) {
    zip_stat_t stat;
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &stat) !=
        0) {
      return Result<std::vector<ZipEntry>>(ArchiveError(archive));
    }
    entries.push_back(
        {.name = stat.name, .size = static_cast<int64_t>(stat.size)});
  }
  return Result<std::vector<ZipEntry>>(std::move(entries));
}

// Returns the content of the entry `index` of `archive`, which has `size`
// bytes.
Result<std::string> EntryContent(zip_t* archive, zip_uint64_t index,
                                 int64_t size) {
  zip_file_t* file = zip_fopen_index(archive, index, 0);
  if (file == nullptr) return Result<std::string>(ArchiveError(archive));
  std::string content(static_cast<size_t>(size), '\0');
  const zip_int64_t read =
      zip_fread(file, content.data(), static_cast<zip_uint64_t>(size));
  if (read != size) {
    Error error{.message = zip_file_strerror(file)};
    zip_fclose(file);
    return Result<std::string>(std::move(error));
  }
  zip_fclose(file);
  return Result<std::string>(std::move(content));
}

// Returns where Python's ZipFile.extract() writes the entry `name` in
// `directory`: without empty, "." and ".." parts.
std::string ExtractPath(const std::string& directory, const std::string& name) {
  std::vector<std::string> parts;
  for (std::string& part : Split(name, "/")) {
    if (!part.empty() && part != "." && part != "..") {
      parts.push_back(std::move(part));
    }
  }
  return pypath::Normpath(pypath::Join({directory, Join(parts, "/")}));
}

}  // namespace

Result<ZipArchive> ZipArchive::Open(const std::string& path, Mode mode) {
  int error_code = 0;
  zip_t* archive = zip_open(
      path.c_str(), mode == Mode::kWrite ? ZIP_CREATE | ZIP_TRUNCATE
                                         : ZIP_CREATE,
      &error_code);
  if (archive == nullptr) return Result<ZipArchive>(CodeError(error_code));
  return Result<ZipArchive>(ZipArchive(archive));
}

ZipArchive::ZipArchive(ZipArchive&& other) noexcept
    : archive_(std::exchange(other.archive_, nullptr)),
      texts_(std::move(other.texts_)) {}

ZipArchive& ZipArchive::operator=(ZipArchive&& other) noexcept {
  if (this != &other) {
    if (archive_ != nullptr) zip_discard(archive_);
    archive_ = std::exchange(other.archive_, nullptr);
    texts_ = std::move(other.texts_);
  }
  return *this;
}

ZipArchive::~ZipArchive() {
  if (archive_ != nullptr) zip_discard(archive_);
}

bool ZipArchive::Contains(const std::string& name) const {
  return zip_name_locate(archive_, name.c_str(), ZIP_FL_ENC_UTF_8) >= 0;
}

Result<void> ZipArchive::AddFile(const std::string& name,
                                 const std::string& source) {
  return AddSource(
      archive_, name,
      zip_source_file(archive_, source.c_str(), 0, ZIP_LENGTH_TO_END),
      ZIP_CM_DEFLATE);
}

Result<void> ZipArchive::AddText(const std::string& name, std::string text) {
  texts_.push_back(std::make_unique<std::string>(std::move(text)));
  const std::string& data = *texts_.back();
  return AddSource(archive_, name,
                   zip_source_buffer(archive_, data.data(), data.size(), 0),
                   ZIP_CM_STORE);
}

Result<void> ZipArchive::Close() {
  if (zip_close(archive_) != 0) {
    const Error error = ArchiveError(archive_);
    zip_discard(archive_);
    archive_ = nullptr;
    return Result<void>(error);
  }
  archive_ = nullptr;
  texts_.clear();
  return Result<void>();
}

Result<std::vector<ZipEntry>> ListZipEntries(const std::string& path) {
  Result<ReadArchive> archive = OpenForReading(path);
  if (!archive.ok()) return Result<std::vector<ZipEntry>>(archive.error());
  return Entries(archive.value().get());
}

Result<void> ExtractZip(const std::string& path,
                        const std::string& directory) {
  Result<ReadArchive> archive = OpenForReading(path);
  if (!archive.ok()) return Result<void>(archive.error());
  const Result<std::vector<ZipEntry>> entries = Entries(archive.value().get());
  if (!entries.ok()) return Result<void>(entries.error());
  for (size_t i = 0; i < entries.value().size(); ++i) {
    const ZipEntry& entry = entries.value()[i];
    const std::string target = ExtractPath(directory, entry.name);
    if (entry.name.ends_with("/")) {
      if (const std::error_code error = platform::EnsureDirs(target)) {
        return Result<void>(OsError(error.value(), target));
      }
      continue;
    }
    const std::string parent = pypath::Dirname(target);
    if (const std::error_code error = platform::EnsureDirs(parent)) {
      return Result<void>(OsError(error.value(), parent));
    }
    const Result<std::string> content =
        EntryContent(archive.value().get(), i, entry.size);
    if (!content.ok()) return Result<void>(content.error());
    if (Result<void> written = WriteFile(target, content.value());
        !written.ok()) {
      return written;
    }
  }
  return Result<void>();
}

}  // namespace cc::util
