// Port of handle_list_runs() of codechecker_client/cmd_line_client.py: the
// `cmd runs` subcommand, which lists the runs of a product
// (cmd-runs-history.md §1).

#ifndef CC_CLIENT_CMD_RUNS_H_
#define CC_CLIENT_CMD_RUNS_H_

#include <string_view>

#include "api/thrift/report_server_types.h"
#include "util/argparse.h"
#include "util/result.h"

namespace cc::client::cmd {

// A sort type of runs and its name as a `cmd runs --sort` value.
struct RunSortTypeName {
  RunSortType::type type;
  std::string_view name;
};

// The values of `cmd runs --sort`, in the order of the RunSortType enum
// (Python: run_sort_type_str()).
inline constexpr RunSortTypeName kRunSortTypes[] = {
    {RunSortType::NAME, "name"},
    {RunSortType::UNRESOLVED_REPORTS, "unresolved_reports"},
    {RunSortType::DATE, "date"},
    {RunSortType::DURATION, "duration"},
    {RunSortType::CC_VERSION, "codechecker_version"}};

// Runs `cmd runs` with the options in `args`. Errors that the result does
// not carry a message for are already logged.
Result<void> ListRuns(const util::argparse::Namespace& args);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_RUNS_H_
