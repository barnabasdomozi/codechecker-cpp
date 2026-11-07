// Port of handle_list_results() of codechecker_client/cmd_line_client.py:
// the `cmd results` subcommand, which lists the reports of runs that match
// the filter options (cmd-results.md §4, §5).

#ifndef CC_CLIENT_CMD_RESULTS_H_
#define CC_CLIENT_CMD_RESULTS_H_

#include "util/argparse.h"
#include "util/result.h"

namespace cc::client::cmd {

// Runs `cmd results` with the options in `args`. Errors that the result
// does not carry a message for are already logged.
Result<void> ListResults(const util::argparse::Namespace& args);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_RESULTS_H_
