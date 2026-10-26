// An authenticated session of a user (Python: session_manager._Session).

#ifndef CC_SERVER_AUTH_SESSION_H_
#define CC_SERVER_AUTH_SESSION_H_

#include <stdint.h>

#include <string>
#include <vector>

namespace cc::server::auth {

// A logged-in user, identified by a token the client sends with each
// request (server-authentication.md §3).
struct Session {
  std::string token;  // 32 hex digits.
  std::string user;
  std::vector<std::string> groups;
  // Whether the user is the configured super user or has the SUPERUSER
  // permission by name (server-authentication.md §2.7).
  bool is_root = false;
  // When the session was last used, in util::LocalNow() time.
  int64_t last_access = 0;
};

}  // namespace cc::server::auth

#endif  // CC_SERVER_AUTH_SESSION_H_
