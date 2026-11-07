// Port of codechecker_client/cli/cmd.py: the `cmd` subcommand, the command
// line client that shows what a server has. Its actions are subcommands of
// their own (`cmd runs`, `cmd results`, ...); each one runs a handler in
// client/cmd/.

#ifndef CC_CLIENT_CLI_CMD_H_
#define CC_CLIENT_CLI_CMD_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::client::cli {

// The help of `cmd` in the subcommand list.
inline constexpr std::string_view kCmdHelp =
    "View analysis results on a running server from the command line.";

void AddCmdArguments(util::argparse::Parser& parser);

// Runs the action of `cmd` in `args` and returns the exit code. Without an
// action, prints the help of `cmd`.
int RunCmd(const util::argparse::Namespace& args);

}  // namespace cc::client::cli

#endif  // CC_CLIENT_CLI_CMD_H_
