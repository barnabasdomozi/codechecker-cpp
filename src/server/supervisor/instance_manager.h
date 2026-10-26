// Port of codechecker_server/instance_manager.py: the list of the user's
// running servers in ~/.codechecker.instances.json, which Python and C++
// servers share (server.md §3, architecture.md §11.9).

#ifndef CC_SERVER_SUPERVISOR_INSTANCE_MANAGER_H_
#define CC_SERVER_SUPERVISOR_INSTANCE_MANAGER_H_

#include <string>
#include <vector>

namespace cc::server::supervisor {

// A running server.
struct Instance {
  int pid = 0;
  std::string hostname;
  std::string workspace;  // Absolute.
  int port = 0;
};

// Returns the running servers of the user (Python: get_instances()). A
// record of this computer counts only if its process is a CodeChecker
// server of the user; records of other computers always count.
std::vector<Instance> GetInstances();

// Records the server `pid` of this computer (Python: register()). Errors
// are logged at debug level.
void RegisterInstance(int pid, const std::string& workspace, int port);

// Removes the record of the server `pid` of this computer (Python:
// unregister()).
void UnregisterInstance(int pid);

}  // namespace cc::server::supervisor

#endif  // CC_SERVER_SUPERVISOR_INSTANCE_MANAGER_H_
