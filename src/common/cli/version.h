// Port of codechecker_common/cli_commands/version.py: the `version`
// subcommand, and the code it shares with `analyzer-version` and
// `web-version` (version.md in docs/reference).

#ifndef CC_COMMON_CLI_VERSION_H_
#define CC_COMMON_CLI_VERSION_H_

#include <string_view>

#include "common/version.h"
#include "util/argparse.h"
#include "util/result.h"

namespace cc::common::cli {

// Reads the version information of one part of the package.
using VersionReader = Result<VersionInfo> (*)();

// The help of `version` in the subcommand list, also its description.
inline constexpr std::string_view kVersionHelp =
    "Print the version of CodeChecker package that is being used.";

// Sets up `parser` for one of the three version commands, which take the
// same options; `description` is shown in its help.
void SetUpVersionParser(util::argparse::Parser& parser,
                        std::string_view description);

// Adds the arguments of `version`.
void AddVersionArguments(util::argparse::Parser& parser);

// Runs `version`: prints the analyzer and the web version. `read_web` is
// null in a build without the web side; the web part is then left out.
// Returns the exit code.
int RunVersion(const util::argparse::Namespace& args,
               VersionReader read_analyzer, VersionReader read_web);

// Runs `analyzer-version` or `web-version`: prints the version of the part
// `part` ("analyzer" or "web"), which `read` reads. Returns the exit code.
int RunPartVersion(const util::argparse::Namespace& args,
                   std::string_view part, VersionReader read);

}  // namespace cc::common::cli

#endif  // CC_COMMON_CLI_VERSION_H_
