// Port of start_server() of codechecker_server/server.py: the main process
// of the server, which prepares the server, starts the API processes and
// the task workers, and then supervises them (server.md §5, §6,
// architecture.md §11.1, §11.2).

#ifndef CC_SERVER_SUPERVISOR_SUPERVISOR_H_
#define CC_SERVER_SUPERVISOR_SUPERVISOR_H_

#include <optional>
#include <string>

#include "server/database/database.h"

namespace cc::server::supervisor {

// The settings of a server start, from the command line.
struct ServerOptions {
  std::string config_directory;
  std::string workspace;  // Absolute.
  std::string listen_address;
  int port = 0;
  bool force_authentication = false;
  bool skip_db_cleanup = false;
  std::string machine_id;
  // Override the counts of server_config.json if set.
  std::optional<int> api_handler_processes;
  std::optional<int> task_worker_processes;
  std::string www_root;         // The web UI files.
  std::string package_version;  // What ServerInfo reports.
};

// Starts the server with the configuration database `config_db` and runs
// it until a signal stops it. Returns the exit code: 128 + the signal
// number after a stop, 1 if the server cannot start. Call it while the
// process has only one thread.
int StartServer(const ServerOptions& options, const db::Database& config_db);

}  // namespace cc::server::supervisor

#endif  // CC_SERVER_SUPERVISOR_SUPERVISOR_H_
