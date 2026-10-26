// Processes other than the running one: worker processes forked from it,
// and processes looked up by their pid (Python: os.fork(), os.waitpid(),
// os.kill(), psutil.Process).

#ifndef CC_PLATFORM_PROCESSES_H_
#define CC_PLATFORM_PROCESSES_H_

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cc::platform {

// Starts a copy of this process that runs `body` and exits with what it
// returns, and returns its pid; -1 if fork() fails. The child starts with
// the default signal handlers and no blocked signals. Buffered output is
// written out before forking and when `body` returns. Call it while the
// process has only one thread.
int ForkChild(const std::function<int()>& body);

// Returns the pid of a child process that has ended, after reaping it;
// nullopt if no child has ended (Python: os.waitpid(-1, os.WNOHANG)).
std::optional<int> ReapEndedChild();

// Returns whether the child process `pid` has ended, after reaping it
// (Python: os.waitpid(pid, os.WNOHANG)).
bool ReapChild(int pid);

// Waits for the child process `pid` to end and reaps it.
void WaitForChild(int pid);

// Sends `signal` to the process `pid`; returns whether it was sent
// (Python: os.kill()).
bool SendSignal(int pid, int signal);

// Returns whether a process `pid` exists (Python: psutil.pid_exists()).
bool ProcessExists(int pid);

// Returns the command line of the process `pid` (Python:
// psutil.Process(pid).cmdline()); nullopt if it cannot be read.
std::optional<std::vector<std::string>> ProcessCommandLine(int pid);

// Returns the name of the user who runs the process `pid` (Python:
// psutil.Process(pid).username()); nullopt if it cannot be found.
std::optional<std::string> ProcessUserName(int pid);

}  // namespace cc::platform

#endif  // CC_PLATFORM_PROCESSES_H_
