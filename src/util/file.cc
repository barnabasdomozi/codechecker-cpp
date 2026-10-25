#include "util/file.h"

#include <stddef.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "util/result.h"
#include "util/strings.h"

namespace cc::util {

Error OsError(int error_number, const std::string& path) {
  return Error{.message = std::format("[Errno {}] {}: {}", error_number,
                                      std::strerror(error_number),
                                      Repr(path))};
}

Result<std::string> ReadFile(const std::string& path) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return Result<std::string>(OsError(errno, path));
  std::string content;
  char buffer[65536];
  size_t count = 0;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, count);
  }
  // fopen() succeeds on a folder; reading it fails with EISDIR.
  const int read_error = std::ferror(file) ? errno : 0;
  std::fclose(file);
  if (read_error != 0) return Result<std::string>(OsError(read_error, path));
  return Result<std::string>(std::move(content));
}

Result<void> WriteFile(const std::string& path, std::string_view content) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) return Result<void>(OsError(errno, path));
  const size_t written = std::fwrite(content.data(), 1, content.size(), file);
  const int write_error = written != content.size() ? errno : 0;
  if (std::fclose(file) != 0 || write_error != 0) {
    return Result<void>(OsError(write_error != 0 ? write_error : errno, path));
  }
  return Result<void>();
}

}  // namespace cc::util
