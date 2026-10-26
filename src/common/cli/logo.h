// The logo of codechecker-cpp, shown above the help pages and when a server
// starts.

#ifndef CC_COMMON_CLI_LOGO_H_
#define CC_COMMON_CLI_LOGO_H_

#include <string>

namespace cc::common::cli {

// Returns the logo and an empty line after it if stdout is a terminal that
// is wide enough for it, else "". The logo is in color unless the NO_COLOR
// environment variable is set and not empty (https://no-color.org).
std::string TerminalLogo();

}  // namespace cc::common::cli

#endif  // CC_COMMON_CLI_LOGO_H_
