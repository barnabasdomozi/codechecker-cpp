#include "server/supervisor/supervisor.h"

#include <signal.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "api/api_version.h"
#include "common/package.h"
#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/processes.h"
#include "platform/signal_receiver.h"
#include "server/auth/permissions.h"
#include "server/database/database.h"
#include "server/database/db_cleanup.h"
#include "server/http/http_server.h"
#include "server/package_data.h"
#include "server/product.h"
#include "server/server_config.h"
#include "server/server_context.h"
#include "server/supervisor/instance_manager.h"
#include "server/tasks/task_manager.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::server::supervisor {
namespace {

constexpr util::Logger kLog("server");

// A worker process of the server and its name in the log.
struct Child {
  int pid;
  std::string name;
};

// The two kinds of worker processes.
enum class WorkerKind { kApi, kTask };

// Returns how the log names the workers of `kind` (Python: the keys of
// child_signal_handler()).
std::string_view WorkerKindName(WorkerKind kind) {
  return kind == WorkerKind::kApi ? "api" : "background";
}

// How many workers of a kind to start.
struct Need {
  WorkerKind kind;
  int count;
};

// Returns the name and number of `signum` as Python prints a signal, for
// example "SIGTERM (15)".
std::string SignalName(int signum) {
  const std::string_view name = signum == SIGINT    ? "SIGINT"
                                : signum == SIGTERM ? "SIGTERM"
                                : signum == SIGHUP  ? "SIGHUP"
                                : signum == SIGCHLD ? "SIGCHLD"
                                                    : "SIGNAL";
  return std::format("{} ({})", name, signum);
}

// Creates server_config.json in `config_directory` if it is missing: from
// the deprecated session_config.json, or from the package's example, and
// returns its path (Python: the first part of start_server()).
std::string PrepareConfigFile(const std::string& config_directory) {
  const std::string root_file =
      util::pypath::Join({config_directory, "root.user"});
  if (util::pypath::Exists(root_file)) {
    kLog.Warning(std::format(
        "The 'root.user' file:  {} is deprecated and ignored. If you want to "
        "setup an initial user with SUPER_USER permission, configure the "
        "super_user field in the server_config.json as described in the "
        "documentation. To get rid off this warning, simply delete the "
        "root.user file.",
        root_file));
  }
  const std::string config_file =
      util::pypath::Join({config_directory, "server_config.json"});
  if (util::pypath::Exists(config_file)) return config_file;
  const std::string session_file =
      util::pypath::Join({config_directory, "session_config.json"});
  const std::string example =
      util::pypath::Join({common::DataFilesDir(), "config",
                          "server_config.json"});
  if (util::pypath::Exists(session_file)) {
    kLog.Info(std::format(
        "Renaming '{}' to '{}'. Please check the example configuration file "
        "('{}') or the user guide for more information.",
        session_file, config_file, example));
    std::rename(session_file.c_str(), config_file.c_str());
  } else {
    kLog.Info(std::format(
        "CodeChecker server's example configuration file created at '{}'",
        config_file));
    (void)platform::CopyFile(example, config_file);
  }
  return config_file;
}

// Deletes the temporary data of the tasks of this machine and drops its
// unfinished tasks; returns how many were dropped (Python:
// _cleanup_incomplete_tasks()).
int64_t CleanupIncompleteTasks(db::LazyConnection& config_db,
                               const ServerOptions& options,
                               std::string_view action) {
  tasks::DestroyAllTemporaryData(options.workspace, options.machine_id);
  Result<db::Connection*> connection = config_db.Get();
  Result<int64_t> dropped =
      connection.ok() ? tasks::DropAllIncompleteTasks(
                            *connection.value(), options.machine_id, action)
                      : Result<int64_t>(connection.error());
  if (!dropped.ok()) {
    kLog.Error(dropped.error().message);
    return 0;
  }
  return dropped.value();
}

// Runs a task worker process (Python: task_executors.main.executor()):
// until the supervisor sends SIGHUP. Termination signals are ignored, so a
// worker can clean up after itself.
int RunTaskWorker() {
  std::error_code error;
  std::optional<platform::SignalReceiver> signals =
      platform::SignalReceiver::Create({SIGHUP, SIGINT, SIGTERM}, &error);
  if (!signals) {
    kLog.Error(std::format("Couldn't receive signals: {}", error.message()));
    return 1;
  }
  while (signals->Wait(std::nullopt) != SIGHUP) {
  }
  kLog.Debug(std::format(
      "Task executor subprocess PID {} exited main loop.",
      platform::ProcessId()));
  return 0;
}

// The main process of a running server. Not thread-safe.
class Supervisor {
 public:
  Supervisor(ServerOptions options, ServerContext* context,
             http::HttpServer* http, std::string address, int api_count,
             int task_count)
      : options_(std::move(options)),
        context_(context),
        http_(http),
        address_(std::move(address)),
        api_count_(api_count),
        task_count_(task_count) {}

  Supervisor(const Supervisor&) = delete;
  Supervisor& operator=(const Supervisor&) = delete;

  // Starts the workers and handles signals until the server stops; returns
  // the exit code.
  int Run(const std::string& config_file, const std::string& secrets_file);

 private:
  // Forks an API handler process, which serves requests with `http_`.
  void SpawnApiProcess();

  // Forks a task worker process.
  void SpawnTaskProcess();

  // Returns the running workers of `kind`.
  std::vector<Child>& ChildrenOf(WorkerKind kind) {
    return kind == WorkerKind::kApi ? api_children_ : task_children_;
  }

  // Starts new workers for those that died (Python: child_signal_handler()).
  void RestartDeadChildren();

  // Returns whether the worker `pid` of `kind` ended, and forgets it. With
  // `reaped`, the caller has reaped it already.
  bool CheckChild(WorkerKind kind, int pid, bool reaped);

  // Stops the workers after the termination signal `signum`; returns false
  // if a second signal forced the stop (Python:
  // termination_signal_handler()).
  bool Shutdown(int signum);

  // Waits until `children` ended. Returns false if a second termination
  // signal killed every worker (Python: forced_termination_signal_handler()).
  bool WaitForChildren(std::vector<Child>& children);

  ServerOptions options_;
  ServerContext* context_;
  http::HttpServer* http_;
  std::string address_;  // "[<host>:<port>]" without the brackets.
  int api_count_;
  int task_count_;
  // Blocks the signals; created before the workers, so their signals wait.
  std::optional<platform::SignalReceiver> signals_;
  std::vector<Child> api_children_;
  std::vector<Child> task_children_;
  int spawned_api_ = 0;
  int spawned_task_ = 0;
  int child_events_ = 0;
  int exit_code_ = 0;
  std::chrono::steady_clock::time_point termination_time_;
};

void Supervisor::SpawnApiProcess() {
  ++spawned_api_;
  http::HttpServer* http = http_;
  const int pid = platform::ForkChild([http] { return http->Serve(); });
  if (pid < 0) return;
  api_children_.push_back(
      {.pid = pid, .name = std::format("CodeChecker-API-{}", spawned_api_)});
  kLog.Debug(std::format("API handler child process {} started!", pid));
}

void Supervisor::SpawnTaskProcess() {
  ++spawned_task_;
  const int pid = platform::ForkChild(&RunTaskWorker);
  if (pid < 0) return;
  task_children_.push_back(
      {.pid = pid, .name = std::format("CodeChecker-Task-{}", spawned_task_)});
  kLog.Debug(std::format("Task child process {} started!", pid));
}

bool Supervisor::CheckChild(WorkerKind kind, int pid, bool reaped) {
  std::vector<Child>& children = ChildrenOf(kind);
  const auto child = std::ranges::find(children, pid, &Child::pid);
  if (child == children.end()) return false;
  if (!reaped && !platform::ReapChild(pid)) return false;
  kLog.Warning(std::format(
      "'{}' child process (PID {}, \"{}\") is not alive anymore!",
      WorkerKindName(kind), pid, child->name));
  children.erase(child);
  return true;
}

void Supervisor::RestartDeadChildren() {
  // Every now and then all children are checked, because signals of
  // children that died at the same time are merged.
  bool force_full_check = false;
  if (child_events_ >= std::min(api_count_, task_count_) / 2) {
    force_full_check = true;
  } else {
    ++child_events_;
  }
  // How many workers of each kind to start, in the order they were added.
  std::vector<Need> needs;
  auto add_need = [&needs](WorkerKind kind) {
    const auto found = std::ranges::find(needs, kind, &Need::kind);
    if (found == needs.end()) {
      needs.push_back({.kind = kind, .count = 1});
    } else {
      ++found->count;
    }
  };
  constexpr WorkerKind kKinds[] = {WorkerKind::kApi, WorkerKind::kTask};
  if (const std::optional<int> pid = platform::ReapEndedChild()) {
    for (const WorkerKind kind : kKinds) {
      if (CheckChild(kind, *pid, /*reaped=*/true)) add_need(kind);
    }
  }
  if (force_full_check) needs.clear();
  if (needs.empty()) {
    for (const WorkerKind kind : kKinds) {
      std::vector<int> pids;
      for (const Child& child : ChildrenOf(kind)) pids.push_back(child.pid);
      std::ranges::sort(pids);
      for (const int pid : pids) {
        if (CheckChild(kind, pid, /*reaped=*/false)) add_need(kind);
      }
    }
  }
  if (force_full_check) {
    child_events_ = 0;
    kLog.Warning(
        "Too many children died since last full status check, performing one "
        "...");
    needs = {
        {.kind = WorkerKind::kApi,
         .count = std::max(0, api_count_ -
                                  static_cast<int>(api_children_.size()))},
        {.kind = WorkerKind::kTask,
         .count = std::max(0, task_count_ -
                                  static_cast<int>(task_children_.size()))}};
  }
  for (const Need& need : needs) {
    kLog.Info(std::format("(Re-)starting {} '{}' child process(es) ...",
                          need.count, WorkerKindName(need.kind)));
    for (int i = 0; i < need.count; ++i) {
      if (need.kind == WorkerKind::kApi) {
        SpawnApiProcess();
      } else {
        SpawnTaskProcess();
      }
    }
  }
}

bool Supervisor::WaitForChildren(std::vector<Child>& children) {
  while (!children.empty()) {
    std::erase_if(children, [](const Child& child) {
      return platform::ReapChild(child.pid);
    });
    if (children.empty()) break;
    const std::optional<int> signum =
        signals_->Wait(std::chrono::milliseconds(100));
    if (signum != SIGINT && signum != SIGTERM) continue;
    // A keypress can send several SIGINTs at once; only a later one counts.
    if (std::chrono::steady_clock::now() - termination_time_ <=
        std::chrono::seconds(2)) {
      continue;
    }
    kLog.Warning(std::format(
        "Termination signal <{}> received a second time, **FORCE** killing "
        "the WEB server on [{}] ...",
        SignalName(*signum), address_));
    for (const Child& child : api_children_) {
      platform::SendSignal(child.pid, SIGKILL);
    }
    for (const Child& child : task_children_) {
      platform::SendSignal(child.pid, SIGKILL);
    }
    exit_code_ = 128 + *signum;
    return false;
  }
  return true;
}

bool Supervisor::Shutdown(int signum) {
  termination_time_ = std::chrono::steady_clock::now();
  exit_code_ = 128 + signum;
  kLog.Info(std::format(
      "Shutting down the WEB server on [{}] ... Please allow some time for "
      "graceful clean-up!",
      address_));
  for (const Child& child : api_children_) {
    kLog.Debug(std::format("SIGINT! API child PID: {} ...", child.pid));
    platform::SendSignal(child.pid, SIGINT);
  }
  if (!WaitForChildren(api_children_)) return false;
  for (const Child& child : task_children_) {
    kLog.Debug(std::format("SIGHUP! Task child PID: {} ...", child.pid));
    platform::SendSignal(child.pid, SIGHUP);
  }
  return WaitForChildren(task_children_);
}

int Supervisor::Run(const std::string& config_file,
                    const std::string& secrets_file) {
  std::error_code error;
  signals_ = platform::SignalReceiver::Create(
      {SIGINT, SIGTERM, SIGHUP, SIGCHLD}, &error);
  if (!signals_) {
    kLog.Error(std::format("Couldn't receive signals: {}", error.message()));
    return 1;
  }
  kLog.Info(
      std::format("Using {} API request handler processes ...", api_count_));
  for (int i = 0; i < api_count_; ++i) SpawnApiProcess();
  kLog.Info(std::format("Using {} Task worker processes ...", task_count_));
  for (int i = 0; i < task_count_; ++i) SpawnTaskProcess();
  kLog.Info(
      std::format("Server waiting for client requests on [{}]", address_));

  while (true) {
    const std::optional<int> signum = signals_->Wait(std::nullopt);
    if (!signum) continue;
    if (*signum == SIGCHLD) {
      RestartDeadChildren();
    } else if (*signum == SIGHUP) {
      kLog.Info("Received signal to reload server configuration ...");
      kLog.Info("Reload server configuration file...");
      const Result<void> reloaded =
          ServerConfig::Reload(config_file, secrets_file);
      if (reloaded.ok()) {
        kLog.Info("Done.");
      } else {
        kLog.Error("Couldn't reload server configuration file");
        kLog.Error(reloaded.error().message);
      }
      kLog.Info("Server configuration reload: Done.");
    } else {
      if (!Shutdown(*signum)) return exit_code_;
      break;
    }
  }
  const int64_t dropped = CleanupIncompleteTasks(
      context_->config_db(), options_,
      "Server shut down, task will be never be completed.");
  if (dropped > 0) {
    kLog.Info(std::format(
        "At server shutdown, dropped {} background tasks that will never be "
        "completed.",
        dropped));
  }
  kLog.Info("CodeChecker server quit (main process).");
  return exit_code_;
}

// Connects to the products and sets up the default permissions (Python:
// the product part of CCSimpleHttpServer.__init__()).
void ConnectProducts(ServerContext& context) {
  Result<db::Connection*> config_db = context.config_db().Get();
  if (!config_db.ok()) {
    kLog.Error(config_db.error().message);
    return;
  }
  const auth::PermissionPlace system = {.config_db = config_db.value(),
                                        .product_id = std::nullopt};
  if (Result<void> done = auth::InitialiseDefaults(auth::Scope::kSystem,
                                                   system);
      !done.ok()) {
    kLog.Error(done.error().message);
  }
  Result<std::vector<Product::Record>> products =
      LoadProducts(*config_db.value());
  if (!products.ok()) {
    kLog.Error(products.error().message);
    return;
  }
  for (const Product::Record& product : products.value()) {
    context.products().Add(product, /*init=*/false, /*count_runs=*/true);
    if (Result<void> done = auth::InitialiseDefaults(
            auth::Scope::kProduct,
            {.config_db = config_db.value(), .product_id = product.id});
        !done.ok()) {
      kLog.Error(done.error().message);
    }
  }
}

}  // namespace

int StartServer(const ServerOptions& options, const db::Database& config_db) {
  kLog.Debug("Starting CodeChecker server...");
  const std::string config_file = PrepareConfigFile(options.config_directory);
  const std::string secrets_file =
      util::pypath::Join({options.config_directory, "server_secrets.json"});

  // Python: webserver_context.get_context(), which fails the start.
  Result<PackageData> package_data = LoadPackageData();
  if (!package_data.ok()) {
    kLog.Error(package_data.error().message);
    return 1;
  }

  db::LazyConnection config_connection(config_db);
  if (!options.skip_db_cleanup) {
    Result<db::Connection*> connection = config_connection.Get();
    Result<std::vector<Product::Record>> products =
        connection.ok() ? LoadProducts(*connection.value())
                        : Result<std::vector<Product::Record>>(
                              connection.error());
    if (!products.ok()) {
      kLog.Error(products.error().message);
    } else {
      db::CleanupProductDatabases(products.value(), options.workspace,
                                  package_data.value().checker_labels);
    }
  } else {
    kLog.Debug("Skipping db_cleanup, as requested.");
  }
  const int64_t dropped = CleanupIncompleteTasks(
      config_connection, options,
      "New server started with the same machine_id, assuming the old server "
      "is dead and won't be able to finish the task.");
  if (dropped > 0) {
    kLog.Info(std::format(
        "At server startup, dropped {} background tasks left behind by a "
        "previous server instance matching machine ID '{}'.",
        dropped, options.machine_id));
  }
  config_connection.Close();

  // Python: CCSimpleHttpServer.__init__().
  kLog.Debug("Initializing HTTP server...");
  kLog.Info(std::format("Server API version: {}", api::kApiVersion));
  Result<ServerConfig> config = ServerConfig::Load(
      config_file, secrets_file, options.force_authentication);
  if (!config.ok()) {
    kLog.Error(config.error().message);
    return 1;
  }
  const int api_count = options.api_handler_processes.value_or(
      config.value().ApiHandlerProcesses());
  const int task_count = options.task_worker_processes.value_or(
      config.value().TaskWorkerProcesses());
  ServerContext context(std::move(config).value(), config_db,
                        options.workspace, options.package_version,
                        std::move(package_data).value());
  if (!options.skip_db_cleanup) {
    kLog.Info("Starting garbage collection on the config database ...");
    context.sessions().DeleteExpiredSessions(std::nullopt);
    kLog.Info("Finished garbage collection on the config database.");
  }
  ConnectProducts(context);

  const std::string key_file =
      util::pypath::Join({options.config_directory, "key.pem"});
  const std::string cert_file =
      util::pypath::Join({options.config_directory, "cert.pem"});
  http::HttpServer http(&context, options.www_root, key_file, cert_file);
  const Result<int> port = http.Bind(options.listen_address, options.port);
  if (!port.ok()) {
    kLog.Error(
        std::format("Couldn't start the server: {}", port.error().message));
    if (port.error().message.starts_with("[Errno 98]")) {
      kLog.Error(std::format(
          "Server can't be started, maybe port number ({}) is already used. "
          "Check the connection parameters. Use the option '-p 0' to find a "
          "free port automatically.",
          options.port));
    }
    return 1;
  }
  if (http.tls()) {
    kLog.Info("Initiating SSL. Server listening on secure socket.");
    kLog.Debug(std::format("Using cert file: {}", cert_file));
    kLog.Debug(std::format("Using key file: {}", key_file));
  } else {
    kLog.Info(std::format("Searching for SSL key at {}, cert at {}, not found!",
                          key_file, cert_file));
    kLog.Info("Falling back to simple, insecure HTTP.");
  }
  const std::string address = std::format(
      "{}:{}", util::BracketIpv6Host(options.listen_address), port.value());

  // Python records the port of the command line, also when it is 0.
  RegisterInstance(platform::ProcessId(), options.workspace, options.port);
  // Workers open their own database connections (architecture.md §11.1).
  context.config_db().Close();
  context.products().TeardownAll();

  Supervisor supervisor(options, &context, &http, address, api_count,
                        task_count);
  const int exit_code = supervisor.Run(config_file, secrets_file);
  UnregisterInstance(platform::ProcessId());
  return exit_code;
}

}  // namespace cc::server::supervisor
