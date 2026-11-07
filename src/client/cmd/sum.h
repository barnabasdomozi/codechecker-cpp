// Port of handle_list_result_types() of
// codechecker_client/cmd_line_client.py: the `cmd sum` subcommand, which
// counts the reports of runs per checker and per severity (cmd-sum.md).

#ifndef CC_CLIENT_CMD_SUM_H_
#define CC_CLIENT_CMD_SUM_H_

#include "util/argparse.h"
#include "util/result.h"

namespace cc::client::cmd {

// Runs `cmd sum` with the options in `args`. Errors that the result does
// not carry a message for are already logged.
Result<void> ListResultTypes(const util::argparse::Namespace& args);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_SUM_H_
