// The running process: its executable, ids, and how it ends on signals and
// internal errors.

#ifndef CC_PLATFORM_PROCESS_H_
#define CC_PLATFORM_PROCESS_H_

#include <stdint.h>

#include <functional>
#include <string>

namespace cc::platform {

// Returns the absolute path of the running executable, with symbolic links
// resolved (/proc/self/exe). Empty if it cannot be read.
std::string ExecutablePath();

// Returns the process id (Python: os.getpid()).
int ProcessId();

// Returns the id of the calling thread as Python's threading.get_ident()
// prints it.
uint64_t ThreadId();

// Makes SIGINT and SIGTERM end the process with exit code 128 + signum, as
// the Python entry point does. Running child sessions (analyzers) get
// SIGTERM first. Buffered output is not flushed.
void ExitOnTerminationSignals();

// Makes writes to a closed pipe fail with EPIPE instead of ending the
// process, as in Python. Child processes get the default behavior back.
void IgnoreBrokenPipes();

// Runs `child` in a forked copy of this process and waits for it,
// forwarding SIGINT, SIGTERM, SIGHUP and SIGCHLD to it (the launcher of
// architecture.md §5.1). The child exits with what `child` returns, after
// writing out buffered output. Returns the child's exit code, or -N if
// signal N ended it, as Python's Popen.wait(). Call it while the process has
// only one thread.
int RunForked(const std::function<int()>& child);

// Makes internal errors (std::terminate(), for example an exception that
// nothing catches) print a stack trace on stdout and exit with code 1, where
// the Python version prints a traceback.
void InstallTerminateHandler();

}  // namespace cc::platform

#endif  // CC_PLATFORM_PROCESS_H_
