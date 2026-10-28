// Port of codechecker_client/cli/store.py: the `store` subcommand, which
// uploads analysis results to a product of a server (store.md).

#ifndef CC_CLIENT_CLI_STORE_H_
#define CC_CLIENT_CLI_STORE_H_

#include <string_view>

#include "util/argparse.h"

namespace cc::client::cli {

// The help of `store` in the subcommand list.
inline constexpr std::string_view kStoreHelp =
    "Save analysis results to a database.";

void AddStoreArguments(util::argparse::Parser& parser);

// Runs `store` and returns the exit code.
int RunStore(const util::argparse::Namespace& args);

}  // namespace cc::client::cli

#endif  // CC_CLIENT_CLI_STORE_H_
