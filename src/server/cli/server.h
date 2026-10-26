// Port of codechecker_server/cli/server.py: the `server` subcommand, which
// starts a server, manages the running ones and checks the databases
// (server.md, server-database.md §3).

#ifndef CC_SERVER_CLI_SERVER_H_
#define CC_SERVER_CLI_SERVER_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::server::cli {

// The help of `server` in the subcommand list.
inline constexpr std::string_view kServerHelp =
    "Start and manage the CodeChecker Web server.";

void AddServerArguments(util::argparse::Parser& parser);

// Runs `server` and returns the exit code.
int RunServer(const util::argparse::Namespace& args);

}  // namespace cc::server::cli

#endif  // CC_SERVER_CLI_SERVER_H_
