// Port of codechecker_web/cli/web_version.py: the `web-version`
// subcommand.

#ifndef CC_WEB_CLI_WEB_VERSION_H_
#define CC_WEB_CLI_WEB_VERSION_H_

#include <string_view>

#include "common/version.h"
#include "util/argparse.h"
#include "util/result.h"

namespace cc::web::cli {

// The help of `web-version` in the subcommand list.
inline constexpr std::string_view kWebVersionHelp =
    "Print the version of CodeChecker server package that is being used.";

void AddWebVersionArguments(util::argparse::Parser& parser);

// Runs `web-version` and returns the exit code.
int RunWebVersion(const util::argparse::Namespace& args);

// Reads the version of the web part from config/web_version.json in the
// package, with the Thrift API version.
Result<common::VersionInfo> ReadWebVersion();

}  // namespace cc::web::cli

#endif  // CC_WEB_CLI_WEB_VERSION_H_
