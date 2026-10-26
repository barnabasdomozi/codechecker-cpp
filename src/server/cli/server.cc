#include "server/cli/server.h"

#include <signal.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "api/thrift/shared_types.h"
#include "common/cli/logo.h"
#include "common/cli/verbose.h"
#include "common/cmd_config.h"
#include "common/package.h"
#include "common/version.h"
#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/processes.h"
#include "platform/subprocess.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "server/database/schema.h"
#include "server/product.h"
#include "server/supervisor/instance_manager.h"
#include "server/supervisor/supervisor.h"
#include "util/argparse.h"
#include "util/logging.h"
#include "util/parallel.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"
#include "web/cli/web_version.h"
#include "web/database_status.h"
#include "web/env.h"

namespace cc::server::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("server");

constexpr std::string_view kDescription =
    "\nThe CodeChecker Web server is used to handle the storage and "
    "navigation of\nanalysis results. A started server can be connected to "
    "via a Web browser, or\nby using the 'CodeChecker cmd' command-line "
    "client.";

constexpr std::string_view kMachineIdHelp = R"(
A unique identifier to be used to identify the machine running subsequent
instances of the "same" server process.
This value is only used internally to maintain normal function and bookkeeping
of executed tasks following an unclean server shutdown, e.g., after a crash or
system-level interference.

If unspecified, defaults to a reasonable default value that is generated from
the computer's hostname, as reported by the operating system.
In most scenarios, there is no need to fine-tune this, except if subsequent
executions of the "same" server is achieved in distinct environments, e.g.,
if the server otherwise is running in a container.
)";

constexpr std::string_view kRootAccountDescription = R"(
Built in super user is not not created by default. You can assign
super user privileges to a user by adding the "super_user" : "user_name"
record to the server_config.json file.
)";

constexpr std::string_view kDatabaseManagementDescription = R"(
WARNING these commands needs to be called with the same workspace and
configuration arguments as the server so the configuration database will be
found which is required for the schema migration. Migration can be done
without a running server but pay attention to use the same arguments which
will be used to start the server.

NOTE:
Before migration it is advised to create a full a backup of the product
databases.
)";

// The PostgreSQL options that need --postgresql.
constexpr std::string_view kPostgresqlOptions[] = {
    "--dbaddress", "--dbport",  "--dbusername", "--dbname",
    "--db-host",   "--db-port", "--db-username", "--db-name"};

// Returns the options of `options` that start with an argument of the
// command line (Python: util.arg_match(options, sys.argv[1:])).
std::vector<std::string> MatchedOptions(
    std::span<const std::string_view> options) {
  const std::vector<std::string>& argv = common::CommandLine();
  std::vector<std::string> matched;
  for (const std::string_view option : options) {
    const bool found =
        std::any_of(argv.begin() + 1, argv.end(), [option](const auto& arg) {
          return !arg.empty() && option.starts_with(arg);
        });
    if (found) matched.emplace_back(option);
  }
  return matched;
}

// Returns the integer argument `dest`, which argparse checked already.
int IntArgument(const argparse::Namespace& args, std::string_view dest) {
  return static_cast<int>(util::ParseInt(args.Get(dest)).value_or(0));
}

// The status of a product database for the status table.
struct ProductStatus {
  std::string endpoint;
  DBStatus::type status;
  std::optional<std::string> schema_version;
  std::string location;
};

// Logs the status table of `statuses` (Python: print_prod_status()).
void PrintProductStatus(const std::vector<ProductStatus>& statuses) {
  const std::string head(db::HeadRevision(db::SchemaKind::kProduct));
  std::vector<std::vector<std::string>> rows;
  for (const ProductStatus& status : statuses) {
    std::string version = status.schema_version.value_or("None");
    if (version == head) version += " (up to date)";
    rows.push_back({status.endpoint,
                    std::string(web::DatabaseStatusMessage(status.status)),
                    status.location, version, head});
  }
  std::ranges::stable_sort(rows, {}, [](const auto& row) { return row[0]; });
  kLog.Info("Status of products:\n" +
            util::twodim::ToString(
                "table",
                {"Product endpoint", "Database status", "Database location",
                 "Schema version in the database",
                 "Schema version in the package"},
                rows));
}

// Returns the status of every product of `config_db`, by endpoint (Python:
// check_product_db_status()). Fails with exit code 1 if the products cannot
// be read.
Result<std::vector<ProductStatus>> CheckProductStatuses(
    const db::Database& config_db, const std::string& workspace) {
  using Statuses = Result<std::vector<ProductStatus>>;
  kLog.Info("Connecting to product databases ...");
  Result<std::unique_ptr<db::Connection>> connection = config_db.Open();
  Result<std::vector<Product::Record>> products =
      connection.ok() ? LoadProducts(*connection.value())
                      : Result<std::vector<Product::Record>>(
                            connection.error());
  if (!products.ok()) {
    kLog.Debug(products.error().message);
    kLog.Error("Failed to get product configurations from the database.");
    kLog.Error("Please check your command arguments.");
    return Statuses(Error{});
  }
  std::vector<ProductStatus> statuses;
  for (const Product::Record& product : products.value()) {
    Result<db::Database> database =
        db::Database::FromConnectionString(product.connection, workspace);
    if (!database.ok()) {
      kLog.Error(std::format(
          "Unable to get the status for product '{}', considering as if the "
          "connection failed.",
          product.endpoint));
      statuses.push_back({.endpoint = product.endpoint,
                          .status = DBStatus::FAILED_TO_CONNECT,
                          .schema_version = std::nullopt,
                          .location = ""});
      continue;
    }
    const DBStatus::type status = db::ConnectDatabase(
        database.value(), db::SchemaKind::kProduct, /*init=*/false);
    statuses.push_back({.endpoint = product.endpoint,
                        .status = status,
                        .schema_version = db::SchemaVersion(database.value()),
                        .location = database.value().Location()});
  }
  return Statuses(std::move(statuses));
}

// A product whose schema is to be created or upgraded.
struct Migration {
  std::string endpoint;
  db::Database database;
  bool init;  // Create a missing schema; otherwise upgrade.
};

// Returns whether to create (`init`) or upgrade the schema of the product
// `endpoint`: always with `force`, otherwise if the user says yes. Logs the
// decision.
bool ConfirmMigration(std::string_view endpoint, bool init, bool force) {
  const std::string question =
      init ? std::format("Do you want to initialize a new schema for '{}'? "
                         "Y(es)/n(o) ",
                         endpoint)
           : std::format("Do you want to upgrade '{}' to new schema? "
                         "Y(es)/n(o) ",
                         endpoint);
  if (force || web::GetUserInput(question)) {
    kLog.Info(std::format("[{}] Schema will be {}...", endpoint,
                          init ? "initialised" : "upgraded"));
    return true;
  }
  kLog.Info(std::format("[{}] No schema {} will be done.", endpoint,
                        init ? "initialization" : "migration"));
  return false;
}

// Creates or upgrades the schema of every product that needs it, asking
// first unless `force` (Python: __db_migration_multiple() with no product
// list). Products that fail are logged; the server still starts.
void MigrateProducts(const db::Database& config_db,
                     const std::string& workspace, bool force) {
  kLog.Info("Preparing schema upgrade for '<all products>'");
  Result<std::vector<ProductStatus>> statuses =
      CheckProductStatuses(config_db, workspace);
  if (!statuses.ok()) return;
  kLog.Warning(
      "Please note after migration only newer CodeChecker versions can be "
      "used to start the server!");
  kLog.Warning("It is advised to make a full backup of your run databases.");
  kLog.Info("========================");
  Result<std::unique_ptr<db::Connection>> connection = config_db.Open();
  std::vector<Migration> migrations;
  for (const ProductStatus& product : statuses.value()) {
    kLog.Info(std::format("Checking: {}", product.endpoint));
    Result<std::optional<std::string>> url =
        connection.ok()
            ? db::QueryValue(*connection.value(),
                             "SELECT connection FROM products "
                             "WHERE endpoint = ?",
                             {product.endpoint})
            : Result<std::optional<std::string>>(connection.error());
    if (!url.ok() || !url.value()) {
      kLog.Error(std::format("Failed to get the configuration for product '{}'",
                             product.endpoint));
      continue;
    }
    Result<db::Database> database =
        db::Database::FromConnectionString(*url.value(), workspace);
    if (!database.ok()) {
      kLog.Error(database.error().message);
      continue;
    }
    const DBStatus::type status = db::ConnectDatabase(
        database.value(), db::SchemaKind::kProduct, /*init=*/false);
    kLog.Info(std::string(web::DatabaseStatusMessage(status)));
    if (status != DBStatus::SCHEMA_MISSING &&
        status != DBStatus::SCHEMA_MISMATCH_OK) {
      continue;
    }
    const bool init = status == DBStatus::SCHEMA_MISSING;
    if (ConfirmMigration(product.endpoint, init, force)) {
      migrations.push_back({.endpoint = product.endpoint,
                            .database = database.value(),
                            .init = init});
    }
  }
  kLog.Info("========================");
  if (!migrations.empty()) {
    const int jobs = std::min(static_cast<int>(migrations.size()),
                              platform::CpuCount());
    kLog.Info(std::format(
        "Initialising/upgrading products using {} concurrent jobs...", jobs));
    std::vector<DBStatus::type> results(migrations.size(), DBStatus::OK);
    util::ParallelFor(jobs, migrations.size(), [&](size_t index) {
      const Migration& migration = migrations[index];
      kLog.Info(std::format(
          "[{}] {}", migration.endpoint,
          migration.init ? "Initialising..." : "Upgrading..."));
      results[index] =
          migration.init
              ? db::ConnectDatabase(migration.database,
                                    db::SchemaKind::kProduct, /*init=*/true)
              : db::UpgradeSchema(migration.database, db::SchemaKind::kProduct,
                                  migration.endpoint);
      kLog.Info(std::format("[{}] Done {}. {}", migration.endpoint,
                            migration.init ? "initialising" : "upgrading",
                            web::DatabaseStatusMessage(results[index])));
    });
    std::vector<std::string> failed;
    for (size_t i = 0; i < migrations.size(); ++i) {
      if (results[i] == DBStatus::OK) continue;
      failed.push_back(std::format("'{}' ({})", migrations[i].endpoint,
                                   web::DatabaseStatusMessage(results[i])));
    }
    if (failed.empty()) {
      kLog.Info("Schema initialisation(s)/upgrade(s) executed successfully.");
    } else {
      kLog.Error(std::format("The following products failed to upgrade: {}",
                             util::Join(failed, ", ")));
    }
  }
  kLog.Info("========================");
}

// Lists, stops or reloads the running servers (Python:
// __instance_management() and __reload_config()).
int ManageInstances(const argparse::Namespace& args) {
  const std::string host_name = platform::HostName();
  const std::vector<supervisor::Instance> instances =
      supervisor::GetInstances();
  if (args.Has("list")) {
    const bool other_hosts = std::ranges::any_of(
        instances, [&host_name](const supervisor::Instance& instance) {
          return instance.hostname != host_name;
        });
    std::vector<std::vector<std::string>> rows;
    for (const supervisor::Instance& instance : instances) {
      if (other_hosts) {
        rows.push_back({instance.workspace,
                        instance.hostname != host_name ? instance.hostname : "",
                        std::to_string(instance.port)});
      } else {
        rows.push_back({instance.workspace, std::to_string(instance.port)});
      }
    }
    util::Print("Your running CodeChecker servers:");
    util::Print(util::twodim::ToString(
        "table",
        other_hosts ? std::vector<std::string>{"Workspace", "Computer host",
                                               "View port"}
                    : std::vector<std::string>{"Workspace", "View port"},
        rows));
    return 0;
  }
  const bool all = args.Has("stop_all");
  const bool reload = args.Has("reload");
  const std::string workspace = util::pypath::Abspath(args.Get("workspace"));
  const std::string port = args.Get("view_port");
  for (const supervisor::Instance& instance : instances) {
    if (instance.hostname != host_name) continue;
    // --stop and --reload are for the server of the workspace and port.
    if (!all && !(std::to_string(instance.port) == port &&
                  util::pypath::Abspath(instance.workspace) == workspace)) {
      continue;
    }
    if (reload) {
      if (!platform::SendSignal(instance.pid, SIGHUP)) {
        kLog.Error(std::format(
            "Couldn't reload configuration file for process PID #{}",
            instance.pid));
        return 1;
      }
      continue;
    }
    // Python: process.kill_process_tree(), then a wait for the server.
    platform::KillProcessTree(instance.pid);
    for (int waited = 0; platform::ProcessExists(instance.pid); ++waited) {
      if (waited > 10) {
        kLog.Warning(std::format(
            "Waiting for process {} to stop has been timed out(timeout = "
            "10)! Process is still running!",
            instance.pid));
        break;
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    kLog.Info(std::format(
        "Stopped CodeChecker server running on port {} in workspace {} "
        "(PID: {})",
        instance.port, instance.workspace, instance.pid));
  }
  return 0;
}

// Checks the configuration database and makes it ready, as the start-up
// sequence of server-database.md §3.2 step 1 does. Returns the exit code
// to stop with, or nullopt to go on.
std::optional<int> CheckConfigDatabase(const db::Database& config_db,
                                       bool force_upgrade) {
  kLog.Info("Checking configuration database ...");
  DBStatus::type status =
      db::ConnectDatabase(config_db, db::SchemaKind::kConfig, /*init=*/false);
  kLog.Info(std::string(web::DatabaseStatusMessage(status)));
  if (status == DBStatus::SCHEMA_MISSING) {
    kLog.Debug("Config database schema is missing, initializing new.");
    status =
        db::ConnectDatabase(config_db, db::SchemaKind::kConfig, /*init=*/true);
    if (status != DBStatus::OK) {
      kLog.Error("Config database initialization failed!");
      kLog.Error("Please check debug logs.");
      return 1;
    }
  }
  if (status == DBStatus::SCHEMA_MISMATCH_NO) {
    kLog.Debug("Configuration database schema mismatch.");
    kLog.Debug("No schema upgrade is possible.");
    return 1;
  }
  if (status == DBStatus::SCHEMA_MISMATCH_OK) {
    kLog.Debug("Configuration database schema mismatch!");
    kLog.Debug("Schema upgrade is possible.");
    kLog.Warning(
        "Please note after migration only newer CodeChecker versions can be "
        "used to start the server!");
    kLog.Warning(
        "It is advised to make a full backup of your configuration database!");
    kLog.Warning(config_db.Location());
    if (!force_upgrade &&
        !web::GetUserInput("Do you want to upgrade to the new schema? "
                           "Y(es)/n(o) ")) {
      kLog.Info("No schema migration was done.");
      return 0;
    }
    kLog.Info("Upgrading schema ...");
    const DBStatus::type upgraded =
        db::UpgradeSchema(config_db, db::SchemaKind::kConfig, "config");
    kLog.Info(std::string(web::DatabaseStatusMessage(upgraded)));
    if (upgraded != DBStatus::OK) {
      kLog.Error("Schema migration failed");
      return static_cast<int>(upgraded);
    }
  }
  if (status == DBStatus::MISSING) {
    kLog.Error("Missing configuration database.");
    kLog.Error("Server can not be started.");
    return 1;
  }
  return std::nullopt;
}

// Creates the Default product, if the configuration database is SQLite, it
// has no products and there is no Default.sqlite yet (server-database.md
// §3.2 step 3). Returns the exit code if that fails.
std::optional<int> CreateDefaultProduct(const db::Database& config_db,
                                        const std::string& workspace) {
  const std::string path = util::pypath::Join({workspace, "Default.sqlite"});
  if (config_db.dialect() != db::Dialect::kSqlite ||
      util::pypath::Exists(path)) {
    return std::nullopt;
  }
  Result<std::unique_ptr<db::Connection>> connection = config_db.Open();
  Result<std::optional<std::string>> any_product =
      connection.ok() ? db::QueryValue(*connection.value(),
                                       "SELECT id FROM products LIMIT 1", {})
                      : Result<std::optional<std::string>>(connection.error());
  if (!any_product.ok()) {
    kLog.Error(any_product.error().message);
    return 1;
  }
  if (any_product.value()) return std::nullopt;
  kLog.Debug("Create default product...");
  const db::Database product = db::Database::Sqlite(path);
  DBStatus::type status =
      db::ConnectDatabase(product, db::SchemaKind::kProduct, /*init=*/false);
  if (status != DBStatus::MISSING) {
    status =
        db::ConnectDatabase(product, db::SchemaKind::kProduct, /*init=*/true);
    kLog.Debug(std::string(web::DatabaseStatusMessage(status)));
    if (status != DBStatus::OK) {
      kLog.Error("Failed to configure default product");
      return 1;
    }
  }
  // The path is relative to the workspace, so the workspace can be moved.
  Result<db::QueryResult> added = connection.value()->Query(
      "INSERT INTO products (endpoint, connection, display_name, description, "
      "report_limit, is_review_status_change_disabled) "
      "VALUES ('Default', ?, 'Default', "
      "'Default product created at server start.', 500000, ?)",
      {db::Database::Sqlite("Default.sqlite").ConnectionString(), false});
  if (!added.ok()) {
    kLog.Error(added.error().message);
    return 1;
  }
  kLog.Info(std::format("Product 'Default' at '{}' created and set up.", path));
  return std::nullopt;
}

// Python: server_init_start(), after the instance commands.
int InitAndStart(const argparse::Namespace& args) {
  if (!args.Has("postgresql")) {
    const std::string dir = util::pypath::Dirname(args.Get("sqlite"));
    if (!util::pypath::IsDir(dir)) (void)platform::EnsureDirs(dir);
  }
  if (args.Has("force_auth")) {
    kLog.Info(
        "'--force-authentication' was passed as a command-line option. The "
        "server will ask for users to authenticate!");
  }
  const std::string workspace = util::pypath::Abspath(args.Get("workspace"));
  const db::Database config_db =
      args.Has("postgresql")
          ? db::Database::Postgresql(
                {.host = args.Get("dbaddress"),
                 .port = IntArgument(args, "dbport"),
                 .user = args.Get("dbusername"),
                 .password = std::nullopt,
                 .database = args.Get("dbname")})
          : db::Database::Sqlite(args.Get("sqlite"));
  const bool force_upgrade = args.Has("force_upgrade");
  if (const std::optional<int> stop =
          CheckConfigDatabase(config_db, force_upgrade)) {
    return *stop;
  }
  // --db-status and --db-upgrade-schema are ignored, as in the reference
  // (known-issues.md #1).
  kLog.Debug("Status was not in the arguments.");
  kLog.Debug("Product upgrade was not in the arguments.");
  if (const std::optional<int> stop =
          CreateDefaultProduct(config_db, workspace)) {
    return *stop;
  }

  Result<std::vector<ProductStatus>> statuses =
      CheckProductStatuses(config_db, workspace);
  if (!statuses.ok()) return statuses.error().exit_code;
  const bool upgrade_available = std::ranges::any_of(
      statuses.value(), [](const ProductStatus& status) {
        return status.status == DBStatus::SCHEMA_MISMATCH_OK ||
               status.status == DBStatus::SCHEMA_MISSING;
      });
  if (upgrade_available) {
    PrintProductStatus(statuses.value());
    kLog.Warning("Multiple products can be upgraded, make a backup!");
    MigrateProducts(config_db, workspace, force_upgrade);
    statuses = CheckProductStatuses(config_db, workspace);
    if (!statuses.ok()) return statuses.error().exit_code;
  }
  PrintProductStatus(statuses.value());
  // Only the first product is looked at, as in the reference
  // (known-issues.md, "Found while writing the reference pages" #22).
  if (!statuses.value().empty() &&
      statuses.value().front().status != DBStatus::OK) {
    kLog.Error("There are some database issues.");
    if (!force_upgrade &&
        !web::GetUserInput("Do you want to start the server? Y(es)/n(o) ")) {
      return 1;
    }
  }

  Result<common::VersionInfo> version = web::cli::ReadWebVersion();
  supervisor::ServerOptions options = {
      .config_directory = args.Get("config_directory"),
      .workspace = workspace,
      .listen_address = args.Get("listen_address"),
      .port = IntArgument(args, "view_port"),
      .force_authentication = args.Has("force_auth"),
      .skip_db_cleanup = args.Get("skip_db_cleanup") == "True",
      .machine_id = args.Has("machine_id")
                        ? args.Get("machine_id")
                        : std::format("{}:{}", platform::HostName(),
                                      args.Get("view_port")),
      .api_handler_processes = std::nullopt,
      .task_worker_processes = std::nullopt,
      .www_root = util::pypath::Join({common::DataFilesDir(), "www"}),
      .package_version =
          version.ok() ? version.value().git_tag.value_or("") : ""};
  if (args.Has("api_handler_processes")) {
    options.api_handler_processes = IntArgument(args, "api_handler_processes");
  }
  if (args.Has("task_worker_processes")) {
    options.task_worker_processes = IntArgument(args, "task_worker_processes");
  }
  return supervisor::StartServer(options, config_db);
}

}  // namespace

void AddServerArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));

  parser.AddArgument(
      {.flags = {"-w", "--workspace"},
       .dest = "workspace",
       .default_value = web::DefaultWorkspace(),
       .help = "Directory where CodeChecker can store analysis result "
               "related data, such as the database."});
  parser.AddArgument(
      {.flags = {"-f", "--config-directory"},
       .dest = "config_directory",
       .default_none = true,
       .help = "Directory where CodeChecker server should read "
               "server-specific configuration (such as authentication "
               "settings, TLS certificate (cert.pem) and key (key.pem)) "
               "from."});
  parser.AddArgument({.flags = {"--machine-id"},
                      .dest = "machine_id",
                      .help = std::string(kMachineIdHelp)});
  parser.AddArgument(
      {.flags = {"--api-handler-processes"},
       .dest = "api_handler_processes",
       .type = argparse::IntType,
       .default_none = true,
       .help = "\nNumber of API request handler processes the server should "
               "start.\nThis setting overrides the server config file "
               "values.\n"});
  parser.AddArgument(
      {.flags = {"--task-worker-processes"},
       .dest = "task_worker_processes",
       .type = argparse::IntType,
       .default_none = true,
       .help = "\nNumber of task worker processes the server should start.\n"
               "These handle report storage tasks (massStoreRun, "
               "massStoreRunAsynchronous).\nThis setting overrides the "
               "server config file values.\n"});
  parser.AddArgument(
      {.flags = {"--host"},
       .dest = "listen_address",
       .default_value = "localhost",
       .help = "The IP address or hostname of the server on which it should "
               "listen for connections. For IPv6 listening, specify an IPv6 "
               "address, such as \"::1\"."});
  parser.AddArgument(
      {.flags = {"-v", "--view-port", "-p", "--port"},
       .dest = "view_port",
       .type = argparse::IntType,
       .metavar = "PORT",
       .default_value = "8001",
       .help = "The port which will be used as listen port for the server."});
  parser.AddArgument(
      {.flags = {"--not-host-only"},
       .dest = "not_host_only",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "If specified, storing and viewing the results will be "
               "possible not only by browsers and clients running locally, "
               "but to everyone, who can access the server over the "
               "Internet. (Equivalent to specifying '--host \"::\"'.)"});
  parser.AddArgument(
      {.flags = {"--skip-db-cleanup"},
       .dest = "skip_db_cleanup",
       .action = argparse::ArgumentAction::kStoreTrue,
       .default_value = "False",
       .help = "Skip performing cleanup jobs on the database like removing "
               "unused files."});
  common::AddConfigOption(parser, 0);

  const int database_group =
      parser.AddArgumentGroup("configuration database arguments", "");
  const int database_modes = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument({.flags = {"--sqlite"},
                      .dest = "sqlite",
                      .metavar = "SQLITE_FILE",
                      .default_none = true,
                      .help = "Path of the SQLite database file to use.",
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
      "specified!");
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
                      .default_value = "codechecker",
                      .help = "Username to use for connection.",
                      .group = postgresql_group});
  parser.AddArgument({.flags = {"--dbname", "--db-name"},
                      .dest = "dbname",
                      .default_value = "config",
                      .help = "Name of the database to use.",
                      .group = postgresql_group});

  const int root_group = parser.AddArgumentGroup(
      "root account arguments", std::string(kRootAccountDescription));
  parser.AddArgument(
      {.flags = {"--force-authentication"},
       .dest = "force_auth",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Force the server to run in authentication requiring mode, "
               "despite the configuration value in 'server_config.json'. "
               "This is needed if you need to edit the product configuration "
               "of a server that would not require authentication otherwise.",
       .group = root_group});

  const int instance_group =
      parser.AddArgumentGroup("running server management", "");
  const int instance_modes = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument(
      {.flags = {"-l", "--list"},
       .dest = "list",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "List the servers that has been started by you.",
       .group = instance_group,
       .exclusive_group = instance_modes});
  parser.AddArgument(
      {.flags = {"-r", "--reload"},
       .dest = "reload",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Sends the CodeChecker server process a SIGHUP signal, "
               "causing it to reread it's configuration files.",
       .group = instance_group,
       .exclusive_group = instance_modes});
  parser.AddArgument(
      {.flags = {"-s", "--stop"},
       .dest = "stop",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Stops the server associated with the given view-port and "
               "workspace.",
       .group = instance_group,
       .exclusive_group = instance_modes});
  parser.AddArgument(
      {.flags = {"--stop-all"},
       .dest = "stop_all",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Stops all of your running CodeChecker server instances.",
       .group = instance_group,
       .exclusive_group = instance_modes});

  const int migration_group = parser.AddArgumentGroup(
      "Database management arguments.",
      std::string(kDatabaseManagementDescription));
  const int migration_modes = parser.AddMutuallyExclusiveGroup();
  parser.AddArgument(
      {.flags = {"--db-status"},
       .dest = "status",
       .help = "Name of the product to get the database status for. Use "
               "'all' to list the database statuses for all of the products.",
       .group = migration_group,
       .exclusive_group = migration_modes});
  parser.AddArgument(
      {.flags = {"--db-upgrade-schema"},
       .dest = "product_to_upgrade",
       .help = "Name of the product to upgrade to the latest database schema "
               "available in the package. Use 'all' to upgrade all of the "
               "products. NOTE: Before migration it is advised to create a "
               "full backup of the product databases.",
       .group = migration_group,
       .exclusive_group = migration_modes});
  parser.AddArgument(
      {.flags = {"--db-force-upgrade"},
       .dest = "force_upgrade",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Force the server to do database migration without user "
               "interaction. NOTE: Please use with caution and before "
               "automatic migration it is advised to create a full backup of "
               "the product databases.",
       .group = migration_group,
       .exclusive_group = migration_modes});

  common::cli::AddVerboseArgument(parser);
}

int RunServer(const argparse::Namespace& parsed) {
  argparse::Namespace args = parsed;
  // Python: __handle(). Options given and defaults cannot be told apart in
  // the namespace, so the command line is looked at.
  argparse::Parser parser(std::format("{} server", common::kProgramName));
  AddServerArguments(parser);
  const std::vector<std::string> postgresql =
      MatchedOptions(kPostgresqlOptions);
  if (!postgresql.empty() && !args.Has("postgresql")) {
    return parser
        .Fail(std::format("argument {}: not allowed without argument "
                          "--postgresql",
                          postgresql.front()))
        .exit_code;
  }
  constexpr std::string_view kHostOptions[] = {"--not-host-only", "--host"};
  const std::vector<std::string> host = MatchedOptions(kHostOptions);
  if (host.size() == 2) {
    return parser
        .Fail("argument --not-host-only: not allowed with argument --host, "
              "as it is a shortcut to --host \"::\"")
        .exit_code;
  }
  if (!host.empty() && host.front() == "--not-host-only") {
    args.Set("listen_address", "::");
  }
  if (!args.Has("config_directory") || args.Get("config_directory").empty()) {
    args.Set("config_directory", args.Get("workspace"));
  }
  if (!args.Has("sqlite") || args.Get("sqlite").empty()) {
    args.Set("sqlite",
             util::pypath::Join({args.Get("workspace"), "config.sqlite"}));
  }
  args.Set("sqlite", util::pypath::Abspath(args.Get("sqlite")));
  const bool instance_manager =
      args.Has("list") || args.Has("stop") || args.Has("stop_all");
  if (!instance_manager && !args.Has("reload")) {
    util::Write(util::Stream::kStdout, common::cli::TerminalLogo());
  }
  kLog.Info(std::format("Workspace directory: {}", args.Get("workspace")));
  kLog.Info(
      std::format("Config directory: {}", args.Get("config_directory")));

  // Python: main().
  if (!instance_manager) {
    for (const auto& [dest, what] :
         {std::pair("workspace", "workspace"),
          std::pair("config_directory", "config")}) {
      const std::string& dir = args.Get(dest);
      if (!util::pypath::Exists(dir)) {
        kLog.Info(std::format("Creating non existing {} directory: {}", what,
                              dir));
        (void)platform::MakeDirs(dir);
      }
    }
  }
  util::SetupLogger(common::cli::Verbose(args), util::Stream::kStdout);
  if (const Result<void> config = common::CheckConfigFile(args);
      !config.ok()) {
    kLog.Error(config.error().message);
    return 1;
  }
  if (instance_manager || args.Has("reload")) return ManageInstances(args);
  return InitAndStart(args);
}

}  // namespace cc::server::cli
