// Port of the session handling of codechecker_server/session_manager.py:
// sessions of logged-in users, kept in memory and in the auth_sessions table
// of the configuration database, and the login methods that create them
// (server-authentication.md §2, §3).
//
// Supported login methods: personal access tokens and the dictionary. LDAP,
// PAM and OAuth are not ported yet; the server configuration turns them off
// at start-up (server_config.h).

#ifndef CC_SERVER_AUTH_SESSION_MANAGER_H_
#define CC_SERVER_AUTH_SESSION_MANAGER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "server/auth/session.h"
#include "server/database/database.h"
#include "server/server_config.h"
#include "util/result.h"

namespace cc::server::auth {

// The sessions of one API process. Each process has its own memory; the
// database is shared, so a session made in one process is found by the
// others. Not thread-safe.
class SessionManager {
 public:
  // Keeps `config` and `config_db`, which must outlive the manager.
  SessionManager(const AuthenticationConfig* config,
                 db::LazyConnection* config_db)
      : config_(config), config_db_(config_db) {}

  SessionManager(const SessionManager&) = delete;
  SessionManager& operator=(const SessionManager&) = delete;

  // Returns whether the server requires authentication.
  bool enabled() const { return config_->enabled; }

  // Returns the live session with `token`, from memory or the database;
  // null if there is none or authentication is off. A session that is no
  // longer alive is deleted (Python: get_session()).
  const Session* GetSession(const std::string& token);

  // Logs in with `auth_string` ("<user>:<secret>") and returns the session;
  // null if the credentials are wrong or authentication is off (Python:
  // create_session()). Fails with the message of a refused login that is
  // not just wrong credentials ("Personal Access Token is expired!").
  Result<const Session*> CreateSession(const std::string& auth_string);

  // Deletes the session with `token` from memory and the database; returns
  // whether that succeeded (Python: invalidate()).
  bool Invalidate(const std::string& token);

  // Removes the session with `token` from the memory of this process
  // (Python: invalidate_local_session()).
  void ForgetSession(const std::string& token);

  // Returns whether `session` is still alive: used within
  // session_lifetime seconds (Python: _Session.is_alive).
  bool IsAlive(const Session& session) const;

  // Deletes the sessions of `user`, or of every user if nullopt, from the
  // database that are no longer alive (Python:
  // db_cleanup.delete_expired_auth_sessions()). Errors are logged.
  void DeleteExpiredSessions(const std::optional<std::string>& user);

 private:
  // A successful login: the user and their groups.
  struct Validation {
    std::string user;
    std::vector<std::string> groups;
  };

  // Returns whether `session` has not been written to the database for
  // refresh_time seconds (Python: _Session.is_refresh_time_expire).
  bool RefreshTimeExpired(const Session& session) const;

  // Updates the last access of `session` in memory and in the database if
  // it is alive and its refresh time passed (Python: _Session.revalidate()).
  void Revalidate(Session& session);

  // Returns the session with `token` from the database; null if there is
  // none (Python: __get_local_session_from_db()).
  std::unique_ptr<Session> LoadSession(const std::string& token);

  // Returns whether `user` is the super user (Python: __is_root_user()).
  bool IsRootUser(const std::string& user);

  // The login methods of __handle_validation(); nullopt means "not
  // accepted".
  Result<std::optional<Validation>> TryPersonalAccessToken(
      const std::string& user, const std::string& secret);
  std::optional<Validation> TryDictionary(const std::string& user,
                                          const std::string& secret,
                                          const std::string& auth_string);

  // Sets the groups of the personal access tokens of `user` (Python:
  // __update_personal_access_token_groups()).
  void UpdateTokenGroups(const std::string& user,
                         const std::vector<std::string>& groups);

  // Python: __cleanup_sessions().
  void CleanupSessions();

  const AuthenticationConfig* config_;
  db::LazyConnection* config_db_;
  std::vector<std::unique_ptr<Session>> sessions_;
  int64_t logins_since_prune_ = 0;
};

}  // namespace cc::server::auth

#endif  // CC_SERVER_AUTH_SESSION_MANAGER_H_
