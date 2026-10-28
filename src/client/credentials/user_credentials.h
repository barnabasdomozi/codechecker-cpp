// Port of codechecker_client/credential_manager.py: the saved credentials
// and session tokens of the client (server-api.md §4.2).

#ifndef CC_CLIENT_CREDENTIALS_USER_CREDENTIALS_H_
#define CC_CLIENT_CREDENTIALS_USER_CREDENTIALS_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "util/result.h"

namespace cc::client::credentials {

// The credentials of the password file and the tokens of the session file
// (web::PasswordFile(), web::SessionFile()), shared with Python clients.
// Copyable; not thread-safe.
class UserCredentials {
 public:
  // Reads both files and logs what it finds, as Python's UserCredentials()
  // does. A missing session file is created; one that is not valid JSON is
  // reset. Fails if the password file has no "credentials" object (Python
  // crashes with a KeyError there).
  static Result<UserCredentials> Load();

  UserCredentials(const UserCredentials& other) = default;
  UserCredentials& operator=(const UserCredentials& other) = default;

  // Whether credentials of the password file are used to log in
  // ("client_autologin", true by default).
  bool autologin() const { return autologin_; }

  // Returns the saved session token of the server `host`:`port`.
  std::optional<std::string> GetToken(std::string_view host,
                                      int64_t port) const;

  // Returns the "user:secret" credential of the server `host`:`port`: the
  // first entry of "host:port", "host", "*:port" and "*".
  std::optional<std::string> GetAuthString(std::string_view host,
                                           int64_t port) const;

  // Saves `token` for the server `host`:`port` and rewrites the session
  // file (Python: save_token()). The error message is the text of Python's
  // OSError.
  Result<void> SaveToken(std::string_view host, int64_t port,
                         const std::string& token);

 private:
  UserCredentials() = default;

  // Credentials by "host:port" or "host" (protocol and product removed).
  std::map<std::string, std::string> credentials_;
  bool autologin_ = true;
  std::string session_file_;
  std::map<std::string, std::string> tokens_;  // By "host:port".
};

}  // namespace cc::client::credentials

#endif  // CC_CLIENT_CREDENTIALS_USER_CREDENTIALS_H_
