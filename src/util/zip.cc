#include "util/zip.h"

#include <stdint.h>
#include <zip.h>

#include <memory>
#include <string>
#include <utility>

#include "util/result.h"

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

}  // namespace

Result<ZipArchive> ZipArchive::Open(const std::string& path, Mode mode) {
  int error_code = 0;
  zip_t* archive = zip_open(
      path.c_str(), mode == Mode::kWrite ? ZIP_CREATE | ZIP_TRUNCATE
                                         : ZIP_CREATE,
      &error_code);
  if (archive == nullptr) {
    zip_error_t error;
    zip_error_init_with_code(&error, error_code);
    const std::string message = zip_error_strerror(&error);
    zip_error_fini(&error);
    return Result<ZipArchive>(Error{.message = message});
  }
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

}  // namespace cc::util
