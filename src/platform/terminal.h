// The terminal the program writes to.

#ifndef CC_PLATFORM_TERMINAL_H_
#define CC_PLATFORM_TERMINAL_H_

#include <optional>

namespace cc::platform {

// Returns the width of the terminal on stdout (Python:
// os.get_terminal_size(1).columns), or nullopt if stdout is not a terminal.
std::optional<int> StdoutTerminalColumns();

}  // namespace cc::platform

#endif  // CC_PLATFORM_TERMINAL_H_
