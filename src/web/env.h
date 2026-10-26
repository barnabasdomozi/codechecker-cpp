// Port of codechecker_web/shared/env.py: settings and checks shared by the
// client and the server.

#ifndef CC_WEB_ENV_H_
#define CC_WEB_ENV_H_

#include <string>
#include <string_view>

namespace cc::web {

// Returns the default workspace of the server, ~/.codechecker (Python:
// get_default_workspace()).
std::string DefaultWorkspace();

// Returns whether only the owner can read or write the file `path`; logs a
// warning if others can (Python: check_file_owner_rw()).
bool CheckFileOwnerReadWrite(const std::string& path);

// Prints the question `message` and returns whether the answer on stdin is
// "y" or "yes", in any case (Python: get_user_input()). The end of the input
// counts as "no".
bool GetUserInput(std::string_view message);

}  // namespace cc::web

#endif  // CC_WEB_ENV_H_
