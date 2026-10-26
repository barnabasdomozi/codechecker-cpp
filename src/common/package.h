// The installed package: the program's name and the package root
// (common-infrastructure.md §1, architecture.md §4).

#ifndef CC_COMMON_PACKAGE_H_
#define CC_COMMON_PACKAGE_H_

#include <string>
#include <string_view>
#include <vector>

namespace cc::common {

// The name of the main program, used in usage and help texts.
inline constexpr std::string_view kProgramName = "codechecker-cpp";

// Returns the package root: the parent of the bin/ folder that holds the
// running program (Python: CC_DATA_FILES_DIR).
std::string DataFilesDir();

// Sets the argument vector of the command, after --config files are spliced
// into it (Python: sys.argv). Call it before other threads start.
void SetCommandLine(std::vector<std::string> args);

// Returns what SetCommandLine() set.
const std::vector<std::string>& CommandLine();

}  // namespace cc::common

#endif  // CC_COMMON_PACKAGE_H_
