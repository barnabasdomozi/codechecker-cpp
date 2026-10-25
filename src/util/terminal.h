// The terminal width, found the way Python finds it.

#ifndef CC_UTIL_TERMINAL_H_
#define CC_UTIL_TERMINAL_H_

#include <optional>

namespace cc::util {

// Returns the COLUMNS environment variable as Python's
// int(os.environ['COLUMNS']) reads it, or nullopt if it is not set or not a
// number. Zero and negative values are returned as they are.
std::optional<int> ColumnsVariable();

// Returns the terminal width like Python's
// shutil.get_terminal_size().columns: COLUMNS if it is positive, else the
// width of the terminal on stdout, else 80.
int TerminalColumns();

}  // namespace cc::util

#endif  // CC_UTIL_TERMINAL_H_
