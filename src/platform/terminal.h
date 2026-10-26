// The terminal the program reads from and writes to.

#ifndef CC_PLATFORM_TERMINAL_H_
#define CC_PLATFORM_TERMINAL_H_

#include <optional>
#include <string>

namespace cc::platform {

// Returns the width of the terminal on stdout (Python:
// os.get_terminal_size(1).columns), or nullopt if stdout is not a terminal.
std::optional<int> StdoutTerminalColumns();

// Returns the next line of stdin without its line end (Python: input());
// nullopt at the end of the input.
std::optional<std::string> ReadStdinLine();

}  // namespace cc::platform

#endif  // CC_PLATFORM_TERMINAL_H_
