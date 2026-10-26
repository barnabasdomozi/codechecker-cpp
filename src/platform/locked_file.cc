#include "platform/locked_file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace cc::platform {

std::optional<LockedFile> LockedFile::Open(const std::string& path, Lock lock,
                                           std::error_code* error) {
  const int fd = open(path.c_str(),
                      (lock == Lock::kShared ? O_RDONLY : O_RDWR) | O_CLOEXEC);
  if (fd < 0) {
    *error = std::error_code(errno, std::generic_category());
    return std::nullopt;
  }
  int result = 0;
  while ((result = flock(fd, lock == Lock::kShared ? LOCK_SH : LOCK_EX)) < 0 &&
         errno == EINTR) {
  }
  if (result < 0) {
    *error = std::error_code(errno, std::generic_category());
    close(fd);
    return std::nullopt;
  }
  return LockedFile(fd);
}

LockedFile::LockedFile(LockedFile&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

LockedFile& LockedFile::operator=(LockedFile&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) close(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

LockedFile::~LockedFile() {
  // Closing the file releases the lock.
  if (fd_ >= 0) close(fd_);
}

std::optional<std::string> LockedFile::Read() {
  if (lseek(fd_, 0, SEEK_SET) < 0) return std::nullopt;
  std::string content;
  char buffer[4096];
  ssize_t size = 0;
  while ((size = read(fd_, buffer, sizeof(buffer))) != 0) {
    if (size < 0) {
      if (errno == EINTR) continue;
      return std::nullopt;
    }
    content.append(buffer, static_cast<size_t>(size));
  }
  return content;
}

std::error_code LockedFile::Replace(std::string_view content) {
  if (lseek(fd_, 0, SEEK_SET) < 0 || ftruncate(fd_, 0) < 0) {
    return std::error_code(errno, std::generic_category());
  }
  while (!content.empty()) {
    const ssize_t size = write(fd_, content.data(), content.size());
    if (size < 0) {
      if (errno == EINTR) continue;
      return std::error_code(errno, std::generic_category());
    }
    content.remove_prefix(static_cast<size_t>(size));
  }
  return {};
}

}  // namespace cc::platform
