#include "util/file.h"

#include <stddef.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <utility>

#include "util/result.h"
#include "util/strings.h"

namespace cc::util {
namespace {

Error OsError(int error_number, const std::string& path) {
  return Error{.message = std::format("[Errno {}] {}: {}", error_number,
                                      std::strerror(error_number),
                                      Repr(path))};
}

}  // namespace

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

}  // namespace cc::util
