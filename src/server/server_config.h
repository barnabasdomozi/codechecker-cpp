// Port of the server_config.json parts of
// codechecker_server/session_manager.py: reading the server's configuration
// file, resolving its variables, and the values the server uses
// (server.md §7, server-authentication.md §1).

#ifndef CC_SERVER_SERVER_CONFIG_H_
#define CC_SERVER_SERVER_CONFIG_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "util/json.h"
#include "util/pyre.h"
#include "util/result.h"

namespace cc::server {

// The settings of the `authentication` object, after the start-up checks
// turned off the methods that cannot be used.
struct AuthenticationConfig {
  // A regex_groups entry: users whose name matches any of the expressions
  // are in the group.
  struct RegexGroup {
    std::string name;
    std::vector<util::Regex> patterns;
  };

  // A method_dictionary.groups entry: the groups of a user.
  struct UserGroups {
    std::string user;
    std::vector<std::string> groups;
  };

  bool enabled = false;
  // Python: auth_config['super_user']; nullopt if missing.
  std::optional<std::string> super_user;
  std::string failed_auth_message;
  // Seconds; nullopt if missing (Python reads them without defaults).
  std::optional<int64_t> session_lifetime;
  std::optional<int64_t> refresh_time;
  std::optional<int64_t> logins_until_cleanup;
  int64_t max_token_expiration = 365;
  bool dictionary_enabled = false;
  // method_dictionary.auths: "user:password" or
  // "user:<hexdigest>:<algorithm>[:<salt>]".
  std::vector<std::string> dictionary_auths;
  // method_dictionary.groups, in the order of the file.
  std::vector<UserGroups> dictionary_groups;
  bool regex_groups_enabled = false;
  std::vector<RegexGroup> regex_groups;
};

// The content of server_config.json (Python: SessionManager.scfg_dict and
// the values SessionManager reads from it). Move-only.
class ServerConfig {
 public:
  // Reads `config_file`, with its "$SECRET:<name>$" values taken from
  // `secrets_file` and its "$ENV:<name>$" values from the environment. Warns
  // if others can read the files. With `force_authentication`,
  // authentication is turned on (--force-authentication). Logs the
  // warnings of the start-up checks (Python: SessionManager.__init__()).
  // The error message is the one the server exits with.
  static Result<ServerConfig> Load(const std::string& config_file,
                                   const std::string& secrets_file,
                                   bool force_authentication);

  // Reads `config_file` and resolves its variables again, for a reload
  // (Python: reload_config()). Only the configuration of the supervisor is
  // reloaded, which has no effect on the requests the API processes serve,
  // as in the reference (server.md §6).
  static Result<void> Reload(const std::string& config_file,
                             const std::string& secrets_file);

  ServerConfig(ServerConfig&& other) = default;
  ServerConfig& operator=(ServerConfig&& other) = default;

  // Returns the number of API handler processes and task worker processes
  // (Python: get_worker_processes()); invalid values are logged and
  // replaced by the defaults.
  int ApiHandlerProcesses() const { return api_handler_processes_; }
  int TaskWorkerProcesses() const { return task_worker_processes_; }

  // Returns whether TCP keepalive is turned on, and its settings; 0 for a
  // setting that is not given (Python: is_keepalive_enabled() and the
  // get_keepalive_*() functions).
  bool KeepaliveEnabled() const;
  int64_t KeepaliveIdle() const;
  int64_t KeepaliveInterval() const;
  int64_t KeepaliveMaxProbe() const;

  const AuthenticationConfig& authentication() const { return auth_; }

 private:
  ServerConfig(util::JsonDocument document, AuthenticationConfig auth,
               int api_handler_processes, int task_worker_processes)
      : document_(std::move(document)),
        auth_(std::move(auth)),
        api_handler_processes_(api_handler_processes),
        task_worker_processes_(task_worker_processes) {}

  // Returns keepalive.<key> as an integer; 0 if it is missing.
  int64_t KeepaliveValue(const char* key) const;

  util::JsonDocument document_;  // The configuration, variables resolved.
  AuthenticationConfig auth_;
  int api_handler_processes_;
  int task_worker_processes_;
};

}  // namespace cc::server

#endif  // CC_SERVER_SERVER_CONFIG_H_
