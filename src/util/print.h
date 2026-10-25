// Output to stdout and stderr (architecture.md §12). The program writes its
// output only through these functions and the logger, never through
// std::cout.
//
// stdout is buffered like Python's sys.stdout: by lines on a terminal,
// in blocks otherwise. stderr is not buffered.

#ifndef CC_UTIL_PRINT_H_
#define CC_UTIL_PRINT_H_

#include <string_view>

namespace cc::util {

enum class Stream { kStdout, kStderr };

// Writes `text` to `stream` as it is (Python: sys.stdout.write(text)).
void Write(Stream stream, std::string_view text);

// Writes `text` and a newline to stdout (Python: print(text)).
void Print(std::string_view text);

// Writes out what is buffered for `stream`.
void Flush(Stream stream);

}  // namespace cc::util

#endif  // CC_UTIL_PRINT_H_
