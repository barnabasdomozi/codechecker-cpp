#include "client/cli/cmd.h"

#include <exception>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "client/cmd/cmd_line_client.h"
#include "client/cmd/history.h"
#include "client/cmd/products.h"
#include "client/cmd/results.h"
#include "client/cmd/runs.h"
#include "client/cmd/sum.h"
#include "common/cli/verbose.h"
#include "common/package.h"
#include "util/argparse.h"
#include "util/logging.h"
#include "util/result.h"
#include "web/cli/postgresql_arguments.h"

namespace cc::client::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");

// The Namespace entries that name the action to run: of `cmd`, and of
// `cmd products` (Python: the `func` default of each action's parser).
constexpr std::string_view kActionDest = "cmd_action";
constexpr std::string_view kProductsActionDest = "products_action";

// The defaults of `cmd products add` that are replaced by values that
// depend on the endpoint.
constexpr std::string_view kSqliteDefault = "<ENDPOINT>.sqlite";
constexpr std::string_view kPostgresqlDefault = "<ENDPOINT>";

// The description in the help of `cmd`.
constexpr std::string_view kCmdDescription =
    "The command-line client is used to connect to a running 'CodeChecker "
    "server' (either remote or local) and quickly inspect analysis results, "
    "such as runs, individual defect reports, compare analyses, etc. Please "
    "see the invidual subcommands for further details.";

// The description in the help of `cmd results`.
constexpr std::string_view kResultsDescription =
    "Show the analysis reports which match the optional filters.\n\nIn JSON "
    "format all available information is shown. In plaintext/table only the "
    "report position, checker name, severity, analyzer name, report message, "
    "detection and review status and Git blame information is displayed.";

// The examples at the end of the help of `cmd results`.
constexpr std::string_view kResultsEpilog = R"(
Example scenario: List analysis results
------------------------------------------------
Get analysis results for a run:
    codechecker-cpp cmd results my_run

Get analysis results for multiple runs:
    codechecker-cpp cmd results my_run1 my_run2

Get analysis results by using regex:
    codechecker-cpp cmd results "my_run*"

Get analysis results for a run and filter the analysis results:
    codechecker-cpp cmd results my_run --severity critical high medium \
        --file "/home/username/my_project/*"

    codechecker-cpp cmd results my_run --review-status confirmed unreviewed \
        --component my_component_name)";

// The examples at the end of the help of `cmd sum`.
constexpr std::string_view kSumEpilog =
    R"(Example scenario: Get checker statistics
------------------------------------------------
Get statistics for a run:
    codechecker-cpp cmd sum -n my_run

Get statistics for all runs filtered by multiple checker names:
    codechecker-cpp cmd sum --all --checker-name "core.*" "deadcode.*"

Get statistics for all runs and only for severity 'high':
    codechecker-cpp cmd sum --all --severity "high")";

// The description in the help of `cmd products`.
constexpr std::string_view kProductsDescription =
    "CodeChecker organises its databases into products. Each product has an "
    "individually configured database which stores the analysis results. "
    "These subcommands are used to manage the products configured by the "
    "server. Please see the individual subcommands for details.";

// The description in the help of `cmd products del`.
constexpr std::string_view kProductsDelDescription =
    "Removes the specified product from the list of products managed by the "
    "server. NOTE: This only removes the association and disconnects the "
    "server from the database -- NO actual ANALYSIS DATA is REMOVED. "
    "Configuration, such as access control, however, WILL BE LOST!";

// Appended to the help of each TIMESTAMP option; it starts with a space.
constexpr std::string_view kTimestampFormat =
    " The format of TIMESTAMP is 'year:month:day:hour:minute:second' (the "
    "\"time\" part can be omitted, in which case midnight (00:00:00) is "
    "used).";

// Appended to the help of the options that filter by detection date; it
// starts with a space.
constexpr std::string_view kDetectionDateHelp =
    " The detection date of a report is the storage date when the report "
    "was stored to the server for the first time.";

// The help of --review-status. Its "R|" prefix keeps the line breaks
// (HelpFormat::kCodeChecker), as in the next two.
constexpr std::string_view kReviewStatusHelp =
    "R|Filter results by review statuses.\n"
    "Reports can be assigned a review status of the following values:\n"
    "- Unreviewed: Nobody has seen this report.\n"
    "- Confirmed: This is really a bug.\n"
    "- False positive: This is not a bug.\n"
    "- Intentional: This report is a bug but we don't want to fix it.";

// The help of --detection-status.
constexpr std::string_view kDetectionStatusHelp =
    "R|Filter results by detection statuses.\n"
    "The detection status is the latest state of a bug report in a run. When "
    "a unique report is first detected it will be marked as New. When the "
    "report is stored again with the same run name then the detection status "
    "changes to one of the following options:\n"
    "- Resolved: when the bug report can't be found after the subsequent "
    "storage.\n"
    "- Unresolved: when the bug report is still among the results after the "
    "subsequent storage.\n"
    "- Reopened: when a Resolved bug appears again.\n"
    "- Off: The bug was reported by a checker that was switched off during "
    "the last analysis which results were stored.\n"
    "- Unavailable: were reported by a checker that does not exists in the "
    "analyzer anymore because it was removed or renamed.";

// The help of --report-status.
constexpr std::string_view kReportStatusHelp =
    "R|Filter results by report statuses.\n"
    "Reports can be assigned a report status of the following values:\n"
    "- Outstanding: Currently detected and still unresolved reports.\n"
    "- Closed: Reports marked as fixed, false positive, or otherwise "
    "resolved.\n";

// Which --url option a parser has (Python: the needs_product_url argument
// of __add_url_arguments()).
enum class UrlKind {
  kNone,     // None, for a parser that does not connect to a server.
  kProduct,  // --url PRODUCT_URL, for the actions on one product.
  kServer,   // --url SERVER_URL, for the actions on the server itself.
};

// Whether a parser has -o/--output (Python: the output_formats argument of
// __add_common_arguments()).
enum class OutputOption { kNone, kDefaultFormats };

// Adds the options that the actions have in common: the --url of `url`,
// -o if `output` says so, and --verbose (Python: __add_common_arguments()).
void AddCommonArguments(argparse::Parser& parser, UrlKind url,
                        OutputOption output) {
  const int group = parser.AddArgumentGroup("common arguments", "");
  if (url == UrlKind::kProduct) {
    parser.AddArgument(
        {.flags = {"--url"},
         .dest = "product_url",
         .metavar = "PRODUCT_URL",
         .default_value = "localhost:8001/Default",
         .help = "The URL of the product which will be accessed by the "
                 "client, in the format of "
                 "'[http[s]://]host:port/Endpoint'.",
         .group = group});
  } else if (url == UrlKind::kServer) {
    parser.AddArgument(
        {.flags = {"--url"},
         .dest = "server_url",
         .metavar = "SERVER_URL",
         .default_value = "localhost:8001",
         .help = "The URL of the server to access, in the format of "
                 "'[http[s]://]host:port'.",
         .group = group});
  }
  if (output == OutputOption::kDefaultFormats) {
    parser.AddArgument(
        {.flags = {"-o", "--output"},
         .dest = "output_format",
         // Python: DEFAULT_OUTPUT_FORMATS.
         .choices = {"plaintext", "rows", "table", "csv", "json"},
         .default_value = "plaintext",
         .help = "The output format(s) to use in showing the data. Mind "
                 "that some output formats are (like 'json') more verbose "
                 "than others (like 'plaintext')."});
  }
  common::cli::AddVerboseArgument(parser, group);
}

// Adds an option with a TIMESTAMP value to `group`.
void AddTimestampArgument(argparse::Parser& parser,
                          std::vector<std::string> flags, std::string dest,
                          std::string help, int group,
                          int exclusive_group = 0) {
  parser.AddArgument({.flags = std::move(flags),
                      .dest = std::move(dest),
                      .type = cmd::ValidTime,
                      .metavar = "TIMESTAMP",
                      .help = std::move(help),
                      .group = group,
                      .exclusive_group = exclusive_group});
}

// Adds a filter option with a list of values to `group`, with its default
// from cmd::DefaultFilterValue() (Python: init_default()).
void AddListFilter(argparse::Parser& parser, std::string flag,
                   std::string dest, std::string metavar, std::string help,
                   int group) {
  std::optional<std::vector<std::string>> default_list =
      cmd::DefaultFilterValue(dest);
  parser.AddArgument({.flags = {std::move(flag)},
                      .dest = std::move(dest),
                      .nargs = argparse::Nargs::kZeroOrMore,
                      .metavar = std::move(metavar),
                      .default_list = std::move(default_list),
                      .help = std::move(help),
                      .group = group});
}

// Adds a store_true filter option to `group`, with its default from
// cmd::DefaultFilterValue().
void AddFlagFilter(argparse::Parser& parser, std::string flag,
                   std::string dest, std::string help, int group) {
  std::optional<std::string> default_value;
  if (const std::optional<std::vector<std::string>> values =
          cmd::DefaultFilterValue(dest)) {
    default_value = values->front();
  }
  parser.AddArgument({.flags = {std::move(flag)},
                      .dest = std::move(dest),
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .default_value = std::move(default_value),
                      .help = std::move(help),
                      .group = group});
}

// Adds the report filter options, with the defaults of
// cmd::DefaultFilterValue(), except that --uniqueing defaults to
// `uniqueing` (Python: __add_filtering_arguments() with
// DEFAULT_FILTER_VALUES, outside diff mode).
void AddFilteringArguments(argparse::Parser& parser,
                           std::string_view uniqueing) {
  const int group = parser.AddArgumentGroup("filter arguments", "");
  const std::string timestamp(kTimestampFormat);
  const std::string detection_date(kDetectionDateHelp);
  parser.AddArgument(
      {.flags = {"--uniqueing"},
       .dest = "uniqueing",
       .choices = {"on", "off"},
       .default_value = std::string(uniqueing),
       .help = "The same bug may appear several times if it is found on "
               "different execution paths, i.e. through different function "
               "calls. By turning on uniqueing a report appears only once "
               "even if it is found on several paths.",
       .group = group});
  AddListFilter(parser, "--report-hash", "report_hash", "REPORT_HASH",
                "Filter results by report hashes.", group);
  AddListFilter(parser, "--review-status", "review_status", "REVIEW_STATUS",
                std::string(kReviewStatusHelp), group);
  AddListFilter(parser, "--detection-status", "detection_status",
                "DETECTION_STATUS", std::string(kDetectionStatusHelp), group);
  AddListFilter(parser, "--severity", "severity", "SEVERITY",
                "Filter results by severities.", group);
  parser.AddArgument(
      {.flags = {"--bug-path-length"},
       .dest = "bug_path_length",
       .help = "Filter results by bug path length. This has the following "
               "format: <minimum_bug_path_length>:<maximum_bug_path_length>. "
               "Valid values are: \"4:10\", \"4:\", \":10\"",
       .group = group});
  AddListFilter(parser, "--tag", "tag", "TAG",
                "Filter results by version tag names.", group);
  AddTimestampArgument(
      parser, {"--outstanding-reports-date", "--open-reports-date"},
      "open_reports_date",
      "Get results which were detected BEFORE the given date and NOT FIXED "
      "BEFORE the given date." + detection_date + timestamp,
      group);
  AddListFilter(parser, "--file", "file_path", "FILE_PATH",
                "Filter results by file path. The file path can contain "
                "multiple * quantifiers which matches any number of "
                "characters (zero or more). So if you have /a/x.cpp and "
                "/a/y.cpp then \"/a/*.cpp\" selects both.",
                group);
  AddListFilter(parser, "--checker-name", "checker_name", "CHECKER_NAME",
                "Filter results by checker names. The checker name can "
                "contain multiple * quantifiers which matches any number of "
                "characters (zero or more). So for example \"*DeadStores\" "
                "will matches \"deadcode.DeadStores\"",
                group);
  AddListFilter(parser, "--checker-msg", "checker_msg", "CHECKER_MSG",
                "Filter results by checker messages.The checker message can "
                "contain multiple * quantifiers which matches any number of "
                "characters (zero or more).",
                group);
  AddListFilter(parser, "--analyzer-name", "analyzer_name", "ANALYZER_NAME",
                "Filter results by analyzer names. The analyzer name can "
                "contain multiple * quantifiers which match any number of "
                "characters (zero or more). So for example \"clang*\" will "
                "match \"clangsa\" and \"clang-tidy\".",
                group);
  AddListFilter(parser, "--component", "component", "COMPONENT",
                "Filter results by source components.", group);
  AddTimestampArgument(
      parser, {"--detected-at"}, "detected_at",
      "DEPRECATED. Use the '--detected-after/--detected-before' options to "
      "filter results by detection date. Filter results by fix date (fixed "
      "after the given date) if the --detection-status filter option is set "
      "only to Resolved otherwise it filters the results by detection date "
      "(detected after the given date)." + timestamp,
      group);
  AddTimestampArgument(
      parser, {"--fixed-at"}, "fixed_at",
      "DEPRECATED. Use the '--fixed-after/--fixed-before' options to filter "
      "results by fix date. Filter results by fix date (fixed before the "
      "given date) if the --detection-status filter option is set only to "
      "Resolved otherwise it filters the results by detection date (detected "
      "before the given date)." + timestamp,
      group);
  AddTimestampArgument(
      parser, {"--detected-before"}, "detected_before",
      "Get results which were detected before the given date." +
          detection_date + timestamp,
      group);
  AddTimestampArgument(
      parser, {"--detected-after"}, "detected_after",
      "Get results which were detected after the given date." +
          detection_date + timestamp,
      group);
  AddTimestampArgument(parser, {"--fixed-before"}, "fixed_before",
                       "Get results which were fixed before the given date." +
                           timestamp,
                       group);
  AddTimestampArgument(parser, {"--fixed-after"}, "fixed_after",
                       "Get results which were fixed after the given date." +
                           timestamp,
                       group);
  AddFlagFilter(parser, "--anywhere-on-report-path",
                "anywhere_on_report_path",
                "Filter reports where the report path not only ends in the "
                "files given by --file or --component, but goes through "
                "them.",
                group);
  AddFlagFilter(parser, "--single-origin-report", "single_origin_report",
                "Filter reports where the report path is entirely in the "
                "files specified by the given --component.",
                group);
  AddListFilter(parser, "--report-status", "report_status", "REPORT_STATUS",
                std::string(kReportStatusHelp), group);
}

// Adds --filter-preset (Python: __add_filter_preset_argument()).
void AddFilterPresetArgument(argparse::Parser& parser) {
  parser.AddArgument(
      {.flags = {"--filter-preset"},
       .dest = "filter_preset_name",
       .metavar = "PRESET_NAME",
       .help = "Use a pre-configured filter preset. The preset is loaded from "
               "the server and applied to the results. Use 'codechecker-cpp "
               "cmd filter-preset list --url <PRODUCT_URL>' to see available "
               "presets."});
}

// Sets up `parser` as the parser of `cmd runs`: its help and options (Python:
// __register_runs() and __add_common_arguments()).
void AddRunsArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_description("List the analysis runs available on the server.");
  const int exclusive = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument(
      {.flags = {"-n", "--name"},
       .dest = "names",
       .nargs = argparse::Nargs::kZeroOrMore,
       .metavar = "RUN_NAME",
       .help = "Names of the analysis runs. If this argument is not supplied "
               "it will show all runs. This has the  following format: "
               "\"<run_name_1> <run_name_2> <run_name_3>\" where run names "
               "can contain multiple * quantifiers which matches any number "
               "of characters (zero or more). So if you have run_1_a_name, "
               "run_2_b_name, run_2_c_name, run_3_d_name then \"run_2* "
               "run_3_d_name\" shows the last three runs.",
       .exclusive_group = exclusive});
  parser.AddArgument({.flags = {"--all-before-run"},
                      .dest = "all_before_run",
                      .metavar = "RUN_NAME",
                      .help = "Get all runs that were stored to the server "
                              "BEFORE the specified one.",
                      .exclusive_group = exclusive});
  parser.AddArgument({.flags = {"--all-after-run"},
                      .dest = "all_after_run",
                      .metavar = "RUN_NAME",
                      .help = "Get all runs that were stored to the server "
                              "AFTER the specified one.",
                      .exclusive_group = exclusive});
  const std::string timestamp(kTimestampFormat);
  AddTimestampArgument(parser, {"--all-after-time"}, "all_after_time",
                       "Get all analysis runs that were stored to the server "
                       "AFTER the given timestamp." + timestamp,
                       /*group=*/0, exclusive);
  AddTimestampArgument(parser, {"--all-before-time"}, "all_before_time",
                       "Get all analysis runs that were stored to the server "
                       "BEFORE the given timestamp." + timestamp,
                       /*group=*/0, exclusive);

  std::vector<std::string> sort_types;
  for (const auto& [type, name] : cmd::kRunSortTypes) {
    sort_types.emplace_back(name);
  }
  parser.AddArgument({.flags = {"--sort"},
                      .dest = "sort_type",
                      .choices = std::move(sort_types),
                      .default_value = "date",
                      .help = "Sort run data by this column."});
  parser.AddArgument({.flags = {"--details"},
                      .dest = "details",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .default_value = "False",
                      .help = "Adds extra details to the run information in "
                              "JSON format, such as the list of enabled "
                              "checkers."});
  parser.AddArgument({.flags = {"--order"},
                      .dest = "sort_order",
                      .choices = {"asc", "desc"},
                      .default_value = "desc",
                      .help = "Sort order of the run data."});
  AddCommonArguments(parser, UrlKind::kProduct, OutputOption::kDefaultFormats);
}

// Sets up `parser` as the parser of `cmd history` (Python:
// __register_run_histories() and __add_common_arguments()).
void AddHistoryArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_description("Show run history for some analysis runs.");
  parser.AddArgument(
      {.flags = {"-n", "--name"},
       .dest = "names",
       .nargs = argparse::Nargs::kZeroOrMore,
       .metavar = "RUN_NAME",
       .help = "Names of the analysis runs to show history for. If this "
               "argument is not supplied it will show the history for all "
               "runs. This has the following format: \"<run_name_1> "
               "<run_name_2> <run_name_3>\" where run names can contain "
               "multiple * quantifiers which matches any number of "
               "characters (zero or more). So if you have run_1_a_name, "
               "run_2_b_name, run_2_c_name, run_3_d_name then \"run_2* "
               "run_3_d_name\" shows history for the last three runs. Use "
               "'codechecker-cpp cmd runs' to get the available runs."});
  AddCommonArguments(parser, UrlKind::kProduct, OutputOption::kDefaultFormats);
}

// Sets up `parser` as the parser of `cmd results`: its help and options
// (Python: __register_results() and __add_common_arguments()).
void AddResultsArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_description(std::string(kResultsDescription));
  parser.set_epilog(std::string(kResultsEpilog));
  parser.AddArgument(
      {.dest = "names",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "RUN_NAMES",
       .help = "Names of the analysis runs to show result summaries of. This "
               "has the following format: <run_name_1> <run_name_2> "
               "<run_name_3> where run names can contain * quantifiers which "
               "matches any number of characters (zero or more). So if you "
               "have run_1_a_name, run_2_b_name, run_2_c_name, run_3_d_name "
               "then \"run_2* run_3_d_name\" selects the last three runs. Use "
               "'codechecker-cpp cmd runs' to get the available runs."});
  parser.AddArgument(
      {.flags = {"--details"},
       .dest = "details",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Get report details for reports such as bug path events, bug "
               "report points etc. Detailed view contains Git information if "
               "it was available and stored for the queried project."});
  AddFilteringArguments(parser, cmd::DefaultFilterValue("uniqueing")->front());
  AddFilterPresetArgument(parser);
  AddCommonArguments(parser, UrlKind::kProduct, OutputOption::kDefaultFormats);
}

// Sets up `parser` as the parser of `cmd sum` (Python: __register_sum()
// and __add_common_arguments()).
void AddSumArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_description("Show checker statistics for some analysis runs.");
  parser.set_epilog(std::string(kSumEpilog));
  const int runs = parser.AddMutuallyExclusiveGroup(
      argparse::ExclusiveGroup::kRequired);
  // The help describes run names separated by ':', which does not work
  // (known-issues #54).
  parser.AddArgument(
      {.flags = {"-n", "--name"},
       .dest = "names",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "RUN_NAME",
       .help = "Names of the analysis runs to show result count breakdown "
               "for. This has the following format: "
               "<run_name_1>:<run_name_2>:<run_name_3> where run names can "
               "contain multiple * quantifiers which matches any number of "
               "characters (zero or more). So if you have run_1_a_name, "
               "run_2_b_name, run_2_c_name, run_3_d_name then "
               "\"run_2*:run_3_d_name\" selects the last three runs. Use "
               "'codechecker-cpp cmd runs' to get the available runs.",
       .exclusive_group = runs});
  parser.AddArgument({.flags = {"-a", "--all"},
                      .dest = "all_results",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .help = "Show breakdown for all analysis runs.",
                      .exclusive_group = runs});
  // Python sets the default in the shared defaults, after `results` and
  // `diff` took theirs.
  AddFilteringArguments(parser, "on");
  AddFilterPresetArgument(parser);
  AddCommonArguments(parser, UrlKind::kProduct, OutputOption::kDefaultFormats);
}

// Sets up `parser` as the parser of `cmd products list` (Python: the
// `list` part of __register_products()).
void AddProductsListArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_description(
      "List the name and basic information about products added to the "
      "server.");
  AddCommonArguments(parser, UrlKind::kServer, OutputOption::kDefaultFormats);
}

// Adds the endpoint argument of `cmd products add` and `del`, whose help is
// `help`.
void AddEndpointArgument(argparse::Parser& parser, std::string help) {
  parser.AddArgument(
      {.dest = "endpoint", .metavar = "ENDPOINT", .help = std::move(help)});
}

// Adds the database options of `cmd products add` (Python: the database
// part of __register_add()).
void AddProductDatabaseArguments(argparse::Parser& parser) {
  const int database_group = parser.AddArgumentGroup(
      "database arguments",
      "NOTE: These database arguments are relative to the server machine, as "
      "it is the server which will make the database connection.");
  const int database_modes = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument(
      {.flags = {"--sqlite"},
       .dest = "sqlite",
       .metavar = "SQLITE_FILE",
       .default_value = std::string(kSqliteDefault),
       .help = "Path of the SQLite database file to use. All paths will be "
               "relative to the server's <WORKSPACE_DIRECTORY>. If an SQLite "
               "server needs to have a different directory, use symlinks "
               "inside the config directory.",
       .group = database_group,
       .exclusive_group = database_modes});
  parser.AddArgument(
      {.flags = {"--postgresql"},
       .dest = "postgresql",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Specifies that a PostgreSQL database is to be used instead "
               "of SQLite. See the \"PostgreSQL arguments\" section on how "
               "to configure the database connection.",
       .group = database_group,
       .exclusive_group = database_modes});

  const int postgresql_group = parser.AddArgumentGroup(
      "PostgreSQL arguments",
      "Values of these arguments are ignored, unless '--postgresql' is "
      "specified! The database specified here must exist, and be "
      "connectible by the server.");
  parser.AddArgument({.flags = {"--dbaddress", "--db-host"},
                      .dest = "dbaddress",
                      .default_value = "localhost",
                      .help = "Database server address.",
                      .group = postgresql_group});
  parser.AddArgument({.flags = {"--dbport", "--db-port"},
                      .dest = "dbport",
                      .type = argparse::IntType,
                      .default_value = "5432",
                      .help = "Database server port.",
                      .group = postgresql_group});
  parser.AddArgument({.flags = {"--dbusername", "--db-username"},
                      .dest = "dbusername",
                      .default_value = std::string(kPostgresqlDefault),
                      .help = "Username to use for connection.",
                      .group = postgresql_group});
  parser.AddArgument({.flags = {"--dbpassword", "--db-password"},
                      .dest = "dbpassword",
                      .default_value = "",
                      .help = "Password to use for authenticating the "
                              "connection.",
                      .group = postgresql_group});
  parser.AddArgument({.flags = {"--dbname", "--db-name"},
                      .dest = "dbname",
                      .default_value = std::string(kPostgresqlDefault),
                      .help = "Name of the database to use.",
                      .group = postgresql_group});
}

// Sets up `parser` as the parser of `cmd products add` (Python:
// __register_add() and __add_common_arguments()).
void AddProductsAddArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_description(
      "Create a new product to be managed by the server by providing the "
      "product's details and database connection.");
  AddEndpointArgument(parser,
                      "The URL endpoint where clients can access the "
                      "analysis results for this product.");
  parser.AddArgument(
      {.flags = {"-n", "--name"},
       .dest = "display_name",
       .help = "A custom display name for the product, which will be shown "
               "in the viewer. This is purely for decoration and user "
               "experience, program calls use the <ENDPOINT>."});
  parser.AddArgument({.flags = {"--description"},
                      .dest = "description",
                      .help = "A custom textual description to be shown "
                              "alongside the product."});
  parser.AddArgument(
      {.flags = {"--report-limit"},
       .dest = "report_limit",
       .type = argparse::IntType,
       .help = "The maximum number of reports allowed to store in one run, "
               "if exceeded, the store action will be rejected."});
  AddProductDatabaseArguments(parser);
  AddCommonArguments(parser, UrlKind::kServer, OutputOption::kNone);
}

// Sets up `parser` as the parser of `cmd products del` (Python:
// __register_del() and __add_common_arguments()).
void AddProductsDelArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_description(std::string(kProductsDelDescription));
  AddEndpointArgument(parser,
                      "The URL endpoint where clients can access the "
                      "analysis results for the product.");
  AddCommonArguments(parser, UrlKind::kServer, OutputOption::kNone);
}

// One action of `cmd`: its parser and its handler.
struct Action {
  std::string_view name;
  std::string_view help;  // One-line help for the list of actions.
  void (*add_arguments)(argparse::Parser& parser);
  // Returns the error to end the command with; its message, if any, is not
  // logged yet.
  Result<void> (*run)(const argparse::Namespace& args);
};

// Adds `actions` to `parser` as its subcommands, listed in the help
// section "available actions"; each one sets `dest` to its name.
void AddActions(argparse::Parser& parser, std::span<const Action> actions,
                std::string_view dest) {
  const int group = parser.AddArgumentGroup("available actions", "");
  parser.AddSubparsers(/*help=*/"", group);
  for (const Action& action : actions) {
    argparse::Parser& subparser =
        parser.AddParser(std::string(action.name), std::string(action.help));
    subparser.SetDefault(std::string(dest), std::string(action.name));
    action.add_arguments(subparser);
  }
}

// Runs the action of `actions` that `args` names in `dest`.
Result<void> RunAction(std::span<const Action> actions, std::string_view dest,
                       const argparse::Namespace& args) {
  for (const Action& action : actions) {
    if (action.name == args.Get(dest)) return action.run(args);
  }
  // The parser knows no other actions.
  std::terminate();
}

// Prints the help of `cmd` or of its action that `arguments`, a command
// line of `cmd` ending in --help, asks for, and returns the error that ends
// the command after it (Python: the entry point parses the command line
// again with --help when no parser set a handler). The launcher has shown
// the logo.
Error PrintCmdHelp(const std::vector<std::string>& arguments) {
  argparse::Parser parser(std::format("{} cmd", common::kProgramName));
  AddCmdArguments(parser);
  const Result<argparse::Namespace> help = parser.ParseArgs(arguments);
  return help.ok() ? Error{.exit_code = 0} : help.error();
}

// Runs `cmd products add` after the checks and the defaults that need the
// command line (Python: the __handle() of the add action).
Result<void> RunProductsAdd(const argparse::Namespace& parsed) {
  argparse::Parser parser(
      std::format("{} cmd products add", common::kProgramName));
  AddProductsAddArguments(parser);
  if (const std::optional<std::string> error =
          web::cli::PostgresqlArgumentError(
              parsed, std::span(common::CommandLine()).subspan(1))) {
    return Result<void>(parser.Fail(*error));
  }
  argparse::Namespace args = parsed;
  // A value given that equals the placeholder is replaced too.
  const std::string endpoint = args.Get("endpoint");
  if (args.Get("sqlite") == kSqliteDefault) {
    args.Set("sqlite", endpoint + ".sqlite");
  }
  for (const std::string_view dest : {"dbusername", "dbname"}) {
    if (args.Get(dest) == kPostgresqlDefault) args.Set(dest, endpoint);
  }
  return cmd::AddProduct(args);
}

// The actions of `cmd products`, in the order of the help.
constexpr Action kProductsActions[] = {
    {"list", "List products available on the server.",
     &AddProductsListArguments, &cmd::ListProducts},
    {"add", "Register a new product to the server.", &AddProductsAddArguments,
     &RunProductsAdd},
    {"del", "Delete a product from the server's products.",
     &AddProductsDelArguments, &cmd::DelProduct},
};

// Sets up `parser` as the parser of `cmd products`, whose actions have
// parsers of their own (Python: __register_products() and
// __add_common_arguments()).
void AddProductsArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kArgumentDefaults);
  parser.set_description(std::string(kProductsDescription));
  parser.set_epilog(
      "Most of these commands require authentication and appropriate access "
      "rights. Please see 'codechecker-cpp cmd login' to authenticate.");
  AddActions(parser, kProductsActions, kProductsActionDest);
  AddCommonArguments(parser, UrlKind::kNone, OutputOption::kNone);
}

// Runs the action of `cmd products` in `args`. Without an action, prints
// the help of `cmd products`.
Result<void> RunProducts(const argparse::Namespace& args) {
  if (!args.Has(kProductsActionDest)) {
    return Result<void>(PrintCmdHelp({"products", "--help"}));
  }
  return RunAction(kProductsActions, kProductsActionDest, args);
}

// The actions, in the order of the help.
constexpr Action kActions[] = {
    {"runs", "List the available analysis runs.", &AddRunsArguments,
     &cmd::ListRuns},
    {"history", "Show run history of multiple runs.", &AddHistoryArguments,
     &cmd::ListRunHistories},
    {"results", "List analysis result (finding) summary for a given run.",
     &AddResultsArguments, &cmd::ListResults},
    {"sum", "Show statistics of checkers.", &AddSumArguments,
     &cmd::ListResultTypes},
    {"products",
     "Access subcommands related to configuring the products managed by a "
     "CodeChecker server.",
     &AddProductsArguments, &RunProducts},
};

}  // namespace

void AddCmdArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kCmdDescription));
  AddActions(parser, kActions, kActionDest);
}

int RunCmd(const argparse::Namespace& args) {
  const Result<void> done = args.Has(kActionDest)
                                ? RunAction(kActions, kActionDest, args)
                                : Result<void>(PrintCmdHelp({"--help"}));
  if (done.ok()) return 0;
  if (!done.error().message.empty()) kLog.Error(done.error().message);
  return done.error().exit_code;
}

}  // namespace cc::client::cli
