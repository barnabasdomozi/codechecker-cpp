#include "server/auth/session_manager.h"

#include <stdint.h>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "server/auth/session.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "server/server_config.h"
#include "util/datetime.h"
#include "util/hash.h"
#include "util/logging.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/uuid.h"

namespace cc::server::auth {
namespace {

constexpr util::Logger kLog("server");

// Returns the groups stored as "a;b;c"; none for "".
std::vector<std::string> SplitGroups(const std::string& groups) {
  if (groups.empty()) return {};
  return util::Split(groups, ";");
}

}  // namespace

bool SessionManager::IsAlive(const Session& session) const {
  return util::LocalNow() - session.last_access <=
         config_->session_lifetime.value_or(0) * util::kMicrosecondsPerSecond;
}

bool SessionManager::RefreshTimeExpired(const Session& session) const {
  const int64_t refresh_time = config_->refresh_time.value_or(0);
  if (refresh_time == 0) return true;
  return util::LocalNow() - session.last_access >
         refresh_time * util::kMicrosecondsPerSecond;
}

void SessionManager::Revalidate(Session& session) {
  if (!IsAlive(session) || !RefreshTimeExpired(session)) return;
  session.last_access = util::LocalNow();
  Result<db::QueryResult> updated = config_db_->Query(
      "UPDATE auth_sessions SET last_access = ? "
      "WHERE user_name = ? AND token = ?",
      {util::FormatDatabaseTime(session.last_access), session.user,
       session.token});
  if (!updated.ok()) {
    kLog.Warning(
        std::format("Couldn't update usage timestamp of {}", session.token));
    kLog.Warning(updated.error().message);
  }
}

bool SessionManager::IsRootUser(const std::string& user) {
  if (config_->super_user == user) return true;
  Result<std::optional<std::string>> found = config_db_->QueryValue(
      "SELECT 1 FROM permissions_system WHERE name = ? "
      "AND permission = 'SUPERUSER' LIMIT 1",
      {user});
  if (!found.ok()) {
    kLog.Error("Couldn't get system permission from database: ");
    kLog.Error(found.error().message);
    return false;
  }
  return found.value().has_value();
}

std::unique_ptr<Session> SessionManager::LoadSession(const std::string& token) {
  Result<db::QueryResult> rows = config_db_->Query(
      "SELECT user_name, groups, last_access FROM auth_sessions "
      "WHERE token = ? LIMIT 1",
      {token});
  if (!rows.ok()) {
    kLog.Error("Couldn't check login in the database: ");
    kLog.Error(rows.error().message);
    return nullptr;
  }
  if (rows.value().rows.empty()) return nullptr;
  const db::Row& row = rows.value().rows.front();
  auto session = std::make_unique<Session>();
  session->token = token;
  session->user = row.Text(0);
  session->groups = SplitGroups(row.Text(1));
  session->is_root = IsRootUser(session->user);
  session->last_access =
      util::ParseDatabaseTime(row.Text(2)).value_or(util::LocalNow());
  return session;
}

const Session* SessionManager::GetSession(const std::string& token) {
  if (!enabled()) return nullptr;
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (session->token == token && IsAlive(*session)) {
      Revalidate(*session);
      return session.get();
    }
  }
  std::unique_ptr<Session> loaded = LoadSession(token);
  if (loaded && IsAlive(*loaded)) {
    Revalidate(*loaded);
    sessions_.push_back(std::move(loaded));
    return sessions_.back().get();
  }
  Invalidate(token);
  return nullptr;
}

Result<std::optional<SessionManager::Validation>>
SessionManager::TryPersonalAccessToken(const std::string& user,
                                       const std::string& secret) {
  using Validated = Result<std::optional<Validation>>;
  Result<db::QueryResult> rows = config_db_->Query(
      "SELECT id, groups, expiration FROM personal_access_tokens "
      "WHERE user_name = ? AND token = ? LIMIT 1",
      {user, secret});
  if (!rows.ok()) {
    kLog.Error("Couldn't check login in the database:");
    kLog.Error(rows.error().message);
    return Validated(std::nullopt);
  }
  if (rows.value().rows.empty()) return Validated(std::nullopt);
  const db::Row& row = rows.value().rows.front();
  const int64_t now = util::LocalNow();
  if (util::ParseDatabaseTime(row.Text(2)).value_or(now) < now) {
    kLog.Info(std::format(
        "User '{}' tried to login with an expired Personal Access Token.",
        user));
    return Validated(Error{.message = "Personal Access Token is expired!"});
  }
  // Python: str(groups).split(";"), so NULL is the group "None".
  std::vector<std::string> groups =
      util::Split(row.OptionalText(1).value_or("None"), ";");
  Result<db::QueryResult> updated = config_db_->Query(
      "UPDATE personal_access_tokens SET last_access = ? WHERE id = ?",
      {util::FormatDatabaseTime(now), row.Int(0)});
  if (!updated.ok()) {
    kLog.Error("Couldn't check login in the database:");
    kLog.Error(updated.error().message);
    return Validated(std::nullopt);
  }
  return Validated(Validation{.user = user, .groups = std::move(groups)});
}

std::optional<SessionManager::Validation> SessionManager::TryDictionary(
    const std::string& user, const std::string& secret,
    const std::string& auth_string) {
  if (!config_->dictionary_enabled) return std::nullopt;
  const auto saved = std::ranges::find_if(
      config_->dictionary_auths, [&user](const std::string& entry) {
        return entry.substr(0, entry.find(':')) == user;
      });
  if (saved == config_->dictionary_auths.end()) return std::nullopt;
  if (*saved != auth_string) {
    // "<user>:<hexdigest>:<algorithm>[:<salt>]".
    const std::vector<std::string> parts = util::Split(*saved, ":", 3);
    if (parts.size() < 3) return std::nullopt;
    const std::string salted = secret + (parts.size() > 3 ? parts[3] : "");
    const std::optional<std::string> digest =
        util::HexDigest(parts[2], salted);
    if (!digest || *digest != parts[1]) return std::nullopt;
  }
  std::vector<std::string> groups;
  for (const AuthenticationConfig::UserGroups& entry :
       config_->dictionary_groups) {
    if (entry.user == user) {
      groups = entry.groups;
      break;
    }
  }
  UpdateTokenGroups(user, groups);
  return Validation{.user = user, .groups = std::move(groups)};
}

void SessionManager::UpdateTokenGroups(const std::string& user,
                                       const std::vector<std::string>& groups) {
  Result<db::QueryResult> updated = config_db_->Query(
      "UPDATE personal_access_tokens SET groups = ? WHERE user_name = ?",
      {util::Join(groups, ";"), user});
  if (!updated.ok()) {
    kLog.Error(
        std::format("Couldn't find personal access token for user {}: {}",
                    user, updated.error().message));
  }
}

Result<const Session*> SessionManager::CreateSession(
    const std::string& auth_string) {
  using Created = Result<const Session*>;
  if (!enabled()) return Created(nullptr);
  ++logins_since_prune_;
  if (logins_since_prune_ >= config_->logins_until_cleanup.value_or(0)) {
    CleanupSessions();
  }
  const std::vector<std::string> parts = util::Split(auth_string, ":", 1);
  if (parts.size() < 2) return Created(nullptr);
  const std::string& user = parts[0];
  const std::string& secret = parts[1];

  // The token of an existing session works as a password.
  Result<std::optional<std::string>> existing = config_db_->QueryValue(
      "SELECT token FROM auth_sessions WHERE user_name = ? AND token = ? "
      "LIMIT 1",
      {user, secret});
  if (!existing.ok()) {
    kLog.Error("Couldn't check login in the database: ");
    kLog.Error(existing.error().message);
  } else if (existing.value()) {
    if (std::unique_ptr<Session> session = LoadSession(*existing.value())) {
      Revalidate(*session);
      sessions_.push_back(std::move(session));
      return Created(sessions_.back().get());
    }
  }

  Result<std::optional<Validation>> token_login =
      TryPersonalAccessToken(user, secret);
  if (!token_login.ok()) return Created(token_login.error());
  std::optional<Validation> validation = std::move(token_login).value();
  if (!validation) validation = TryDictionary(user, secret, auth_string);
  if (!validation) return Created(nullptr);
  if (config_->regex_groups_enabled) {
    for (const AuthenticationConfig::RegexGroup& group :
         config_->regex_groups) {
      const bool matches = std::ranges::any_of(
          group.patterns, [&validation](const util::Regex& pattern) {
            return pattern.Search(validation->user).has_value();
          });
      if (matches && std::ranges::find(validation->groups, group.name) ==
                         validation->groups.end()) {
        validation->groups.push_back(group.name);
      }
    }
  }

  DeleteExpiredSessions(validation->user);
  std::optional<std::string> token = util::RandomHexToken(32);
  if (!token) {
    return Created(Error{.message = "Couldn't generate a session token."});
  }
  auto session = std::make_unique<Session>();
  session->token = std::move(*token);
  session->user = validation->user;
  session->groups = validation->groups;
  session->is_root = IsRootUser(session->user);
  session->last_access = util::LocalNow();
  Result<db::QueryResult> inserted = config_db_->Query(
      "INSERT INTO auth_sessions (user_name, token, groups, last_access) "
      "VALUES (?, ?, ?, ?)",
      {session->user, session->token, util::Join(session->groups, ";"),
       util::FormatDatabaseTime(session->last_access)});
  if (!inserted.ok()) {
    kLog.Error("Couldn't store or update login record in database:");
    kLog.Error(inserted.error().message);
  }
  sessions_.push_back(std::move(session));
  return Created(sessions_.back().get());
}

void SessionManager::ForgetSession(const std::string& token) {
  const auto found = std::ranges::find_if(
      sessions_, [&token](const std::unique_ptr<Session>& session) {
        return session->token == token;
      });
  if (found != sessions_.end()) sessions_.erase(found);
}

bool SessionManager::Invalidate(const std::string& token) {
  ForgetSession(token);
  Result<db::QueryResult> deleted =
      config_db_->Query("DELETE FROM auth_sessions WHERE token = ?", {token});
  if (!deleted.ok()) {
    kLog.Error(std::format("Couldn't invalidate session for token {}", token));
    kLog.Error(deleted.error().message);
    return false;
  }
  return true;
}

void SessionManager::CleanupSessions() {
  logins_since_prune_ = 0;
  std::erase_if(sessions_, [this](const std::unique_ptr<Session>& session) {
    return RefreshTimeExpired(*session);
  });
  std::vector<std::string> dead;
  for (const std::unique_ptr<Session>& session : sessions_) {
    if (!IsAlive(*session)) dead.push_back(session->token);
  }
  for (const std::string& token : dead) Invalidate(token);
}

void SessionManager::DeleteExpiredSessions(
    const std::optional<std::string>& user) {
  if (user) {
    kLog.Debug(std::format(
        "Cleaning up expired auth sessions of user '{}' ...", *user));
  } else {
    kLog.Debug("Cleaning up all expired auth sessions ...");
  }
  const std::string cutoff = util::FormatDatabaseTime(
      util::LocalNow() -
      config_->session_lifetime.value_or(0) * util::kMicrosecondsPerSecond);
  Result<db::QueryResult> deleted =
      user ? config_db_->Query(
                 "DELETE FROM auth_sessions "
                 "WHERE user_name = ? AND last_access < ?",
                 {*user, cutoff})
           : config_db_->Query(
                 "DELETE FROM auth_sessions WHERE last_access < ?", {cutoff});
  if (!deleted.ok()) {
    kLog.Error("Failed to cleanup expired auth sessions from the database:");
    kLog.Error(deleted.error().message);
    return;
  }
  kLog.Debug("Finished cleanup of expired auth sessions.");
}

}  // namespace cc::server::auth
