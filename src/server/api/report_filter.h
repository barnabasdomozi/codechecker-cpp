// Port of the query building functions of
// codechecker_server/api/report_server.py: the SQL of the report, compare,
// run and run history filters of the CodeCheckerService (architecture.md
// §11.4). They return their errors as ApiErrors, which the handler throws
// as RequestFailed (cppguide.md W1).

#ifndef CC_SERVER_API_REPORT_FILTER_H_
#define CC_SERVER_API_REPORT_FILTER_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "api/thrift/report_server_types.h"
#include "server/api/request_failed.h"
#include "server/database/connection.h"
#include "server/database/sql.h"

namespace cc::server::api {

// The tables a query joins to "reports" (Python: join_tables).
struct JoinedTables {
  bool checkers = false;
  bool files = false;
  bool runs = false;
  bool run_histories = false;
};

// A report filter as SQL (Python: the result of process_report_filter()).
struct ReportFilterSql {
  db::Sql condition = {};
  JoinedTables joins = {};
  // Whether `condition` refers to the report_annotations table.
  bool uses_annotations = false;
};

// The name of the source component of the files that are in no other
// component (Python: GEN_OTHER_COMPONENT_NAME).
inline constexpr std::string_view kOtherComponentName =
    "Other (auto-generated)";

// The path patterns of a source component.
struct ComponentValues {
  std::vector<std::string> include;  // The "+<pattern>" lines.
  std::vector<std::string> skip;     // The "-<pattern>" lines.
};

// Returns the patterns in the value `value` of a source component (Python:
// get_component_values()).
ComponentValues ParseComponentValues(std::string_view value);

// Returns `filter` with '*' as the wildcard of LIKE (Python: conv()).
std::string Conv(std::string_view filter);

// Returns `text` with the LIKE wildcards escaped by '\' (Python:
// escape_like(text, '\\')).
std::string EscapeLike(std::string_view text);

// Returns the Unix time `timestamp` as a query parameter (Python:
// datetime.fromtimestamp(timestamp)).
std::string TimeParam(int64_t timestamp);

// Returns the local time `text` written as "%Y-%m-%d %H:%M:%S.%f" (Python:
// datetime.strptime(text, '%Y-%m-%d %H:%M:%S.%f')); fails with GENERAL and
// Python's message if it is not in that form.
ApiResult<int64_t> ParseTime(const std::string& text);

// Returns the condition that reports of `table` ("reports" or a subquery)
// were open at `date`, a column or a parameter (Python:
// get_open_reports_date_filter_query()).
db::Sql OpenReportsAt(std::string_view table, const db::Sql& date);

// Returns the condition that a report is outstanding: unreviewed or
// confirmed, and new, unresolved or reopened (Python: the reportStatus
// part of process_report_filter()).
db::Sql OutstandingCondition();

// Returns the SQL of the report filter `filter` of the runs `run_ids`
// compared with `cmp_data`, which may be null (Python:
// process_report_filter()). With `keep_all_annotations`, the annotation
// filter keeps the annotation rows of other keys, for a HAVING clause on
// the pivoted annotations. Queries `db` for the source components. Fails
// with DATABASE if the query or the diff type fails, with GENERAL if a date
// or a status of `filter` is invalid.
ApiResult<ReportFilterSql> ProcessReportFilter(
    db::Connection& db, const std::vector<int64_t>& run_ids,
    const ReportFilter& filter, const CompareData* cmp_data,
    bool keep_all_annotations = false);

// Adds `filter` to `select`, which selects from "reports": joins the tables
// it needs that `joined` does not have, and adds its condition (Python:
// apply_report_filter()).
void ApplyReportFilter(const ReportFilterSql& filter,
                       const JoinedTables& joined, db::Select* select);

// Adds the conditions of `filter` on the "runs" table to `select` (Python:
// process_run_filter()). Queries `db` for the dates of the runs named by
// beforeRun and afterRun; fails with DATABASE if that fails.
ApiResult<void> ProcessRunFilter(db::Connection& db, const RunFilter& filter,
                                 db::Select* select);

// Returns whether `filter` has no field set: the client sent none (Python:
// None).
bool IsUnset(const RunFilter& filter);

// Adds the conditions of `run_ids` and `filter` on the "run_histories" table
// to `select` (Python: process_run_history_filter()).
void ProcessRunHistoryFilter(const std::vector<int64_t>& run_ids,
                             const RunHistoryFilter& filter,
                             db::Select* select);

// Returns the names of `statuses`; fails with GENERAL for an unknown one.
ApiResult<std::vector<std::string>> DetectionStatusNames(
    const std::vector<DetectionStatus::type>& statuses);

}  // namespace cc::server::api

#endif  // CC_SERVER_API_REPORT_FILTER_H_
