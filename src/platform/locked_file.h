// Files shared between processes, read and rewritten under flock() (Python:
// portalocker, which uses flock() on Linux), such as the list of running
// servers.

#ifndef CC_PLATFORM_LOCKED_FILE_H_
#define CC_PLATFORM_LOCKED_FILE_H_

#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace cc::platform {

// An open file with a lock on it; the lock is released when it is
// destroyed. Not thread-safe.
class LockedFile {
 public:
  enum class Lock {
    kShared,     // For reading (Python: LOCK_SH).
    kExclusive,  // For reading and rewriting (Python: LOCK_EX).
  };

  // Opens the existing file `path` and waits for the lock `lock` on it. On
  // failure, returns nullopt and sets `*error`.
  static std::optional<LockedFile> Open(const std::string& path, Lock lock,
                                        std::error_code* error);

  LockedFile(LockedFile&& other) noexcept;
  LockedFile& operator=(LockedFile&& other) noexcept;
  ~LockedFile();

  // Returns the content of the file; nullopt if it cannot be read.
  std::optional<std::string> Read();

  // Replaces the content of the file with `content`. Needs kExclusive.
  std::error_code Replace(std::string_view content);

 private:
  explicit LockedFile(int fd) : fd_(fd) {}

  int fd_;  // -1 after a move.
};

}  // namespace cc::platform

#endif  // CC_PLATFORM_LOCKED_FILE_H_
