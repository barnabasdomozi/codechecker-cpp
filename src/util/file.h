// Reading and writing files.

#ifndef CC_UTIL_FILE_H_
#define CC_UTIL_FILE_H_

#include <string>
#include <string_view>

#include "util/result.h"

namespace cc::util {

// Returns the error for a failed system call on `path` with the text of
// Python's OSError, for example
// "[Errno 2] No such file or directory: 'x.json'".
Error OsError(int error_number, const std::string& path);

// Returns the bytes of the file at `path`. On failure, the error message is
// the text of Python's OSError.
Result<std::string> ReadFile(const std::string& path);

// Writes `content` to the file at `path`, replacing what it had. On
// failure, the error message is the text of Python's OSError.
Result<void> WriteFile(const std::string& path, std::string_view content);

}  // namespace cc::util

#endif  // CC_UTIL_FILE_H_
