// Port of handle_list_run_histories() of
// codechecker_client/cmd_line_client.py: the `cmd history` subcommand,
// which lists the stores into runs (cmd-runs-history.md §2).

#ifndef CC_CLIENT_CMD_HISTORY_H_
#define CC_CLIENT_CMD_HISTORY_H_

#include "util/argparse.h"
#include "util/result.h"

namespace cc::client::cmd {

// Runs `cmd history` with the options in `args`. Errors that the result
// does not carry a message for are already logged.
Result<void> ListRunHistories(const util::argparse::Namespace& args);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_HISTORY_H_
