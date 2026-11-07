// Port of the shared parts of codechecker_client/cmd_line_client.py: what
// the cmd subcommands have in common, such as fetching all runs or reports
// page by page, and the report filter of the filter options
// (cmd-results.md §2).

#ifndef CC_CLIENT_CMD_CMD_LINE_CLIENT_H_
#define CC_CLIENT_CMD_CMD_LINE_CLIENT_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/thrift/report_server_types.h"
#include "client/client.h"
#include "util/argparse.h"
#include "util/result.h"

namespace cc::client::cmd {

// Returns the default of the filter option `dest` as its list of values
// (store_true options as "False") for the options that have a default, and
// nullopt for the others (Python: filter_defaults.DEFAULT_FILTER_VALUES).
std::optional<std::vector<std::string>> DefaultFilterValue(
    std::string_view dest);

// Sets up the logger of a cmd subcommand: log lines go to stdout only for
// `-o table`, so that the other formats can be read by programs (Python:
// the init_logger() call of the handlers).
void SetupCmdLogger(const util::argparse::Namespace& args);

// Converts a TIMESTAMP option value, "year:month:day[:hour[:minute
// [:second]]]", for argparse (Python: cmd.valid_time()). The result is
// meant for TimestampOf(); the error message is that of Python's
// ValueError, for example "month must be in 1..12".
Result<std::string> ValidTime(const std::string& value);

// Returns the Unix time of `value`, a value converted by ValidTime(), taken
// as local time (Python: int(str_to_timestamp(value))).
Result<int64_t> TimestampOf(const std::string& value);

// Returns the name of `value` in `names`, the generated name table of an
// API enum, for example _Severity_VALUES_TO_NAMES (Python:
// <Enum>._VALUES_TO_NAMES[value]); the number itself if it has no name.
std::string EnumName(const std::map<int, const char*>& names, int value);

// Returns every run that `filter` selects, in the order of `sort_mode`,
// asking for one page of MAX_QUERY_SIZE runs at a time (Python:
// get_run_data()). Without `sort_mode`, the runs are in the server's
// default order: newest first.
Result<std::vector<RunData>> GetRunData(
    ResultsClient& client, const RunFilter& filter,
    const std::optional<RunSortMode>& sort_mode);

// Returns every report of the runs `run_ids` that `filter` selects, in the
// server's default order (by severity, highest first), with the bug path,
// comments and blame information if `details` (Python: get_run_results()
// without sorting and compare data).
Result<std::vector<ReportData>> GetRunResults(
    ResultsClient& client, const std::vector<int64_t>& run_ids,
    const ReportFilter& filter, bool details);

// Fails, after logging which ones, if any of the source components `names`
// does not exist (Python: check_existing_source_components()).
Result<void> CheckExistingSourceComponents(
    ResultsClient& client, const std::vector<std::string>& names);

// Logs a warning for each deprecated filter option in `args` (Python:
// check_deprecated_arg_usage()).
void CheckDeprecatedArgUsage(const util::argparse::Namespace& args);

// Returns the report filter of the filter options in `args`, or the filter
// of the preset that --filter-preset names (Python: parse_report_filter()).
// Fails, after logging why, if --filter-preset comes with other filter
// options or names no preset, and with Python's KeyError for an unknown
// severity, review status, detection status or report status.
Result<ReportFilter> ParseReportFilter(ResultsClient& client,
                                       const util::argparse::Namespace& args);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_CMD_LINE_CLIENT_H_
