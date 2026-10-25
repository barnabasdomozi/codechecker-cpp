// Reading files.

#ifndef CC_UTIL_FILE_H_
#define CC_UTIL_FILE_H_

#include <string>

#include "util/result.h"

namespace cc::util {

// Returns the bytes of the file at `path`. On failure, the error message is
// the text of Python's OSError, for example
// "[Errno 2] No such file or directory: 'x.json'".
Result<std::string> ReadFile(const std::string& path);

}  // namespace cc::util

#endif  // CC_UTIL_FILE_H_
