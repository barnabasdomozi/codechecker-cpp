// Child processes, run the way Python's subprocess module runs them
// (libraries.md, "Operating system API"): an exact environment, a working
// folder, output read from both pipes at the same time, and a timeout that
// can kill the whole process tree.

#ifndef CC_PLATFORM_SUBPROCESS_H_
#define CC_PLATFORM_SUBPROCESS_H_

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cc::platform {

// Where an output stream of the child goes (Python: subprocess.PIPE,
// DEVNULL, STDOUT, or None for inherited).
enum class Output {
  kPipe,     // Captured into RunResult.
  kInherit,  // The parent's stream.
  kDevNull,
  kStdout,   // stderr only: merged into the captured stdout.
};

struct RunOptions {
  // The working folder of the child; empty: the current folder.
  std::string cwd = {};
  // The environment as "KEY=VALUE" strings; nullopt: the current one. The
  // program is looked up in its PATH, as Python does.
  std::optional<std::vector<std::string>> env = std::nullopt;
  // Written to stdin, which is then closed; nullopt: stdin is inherited.
  std::optional<std::string> input = std::nullopt;
  Output stdout_mode = Output::kPipe;
  Output stderr_mode = Output::kPipe;
  // Runs the child in a new session and process group (Python:
  // preexec_fn=os.setsid). Such children are sent SIGTERM when the program
  // ends on SIGINT or SIGTERM.
  bool new_session = false;
  // If the child runs longer than this, `on_timeout` is called once with
  // its pid, from the calling thread; the child is still waited for.
  std::optional<std::chrono::milliseconds> timeout = std::nullopt;
  std::function<void(int pid)> on_timeout = {};
};

struct RunResult {
  // The errno of a failed start (Python: the OSError of Popen), and the path
  // it refers to: the program, or the working folder. 0 if it started.
  int start_error = 0;
  std::string start_error_path = {};
  // The exit code, or -N if a signal N ended the child (Python: returncode).
  int return_code = 0;
  std::string out = {};  // The bytes of the captured stdout.
  std::string err = {};  // The bytes of the captured stderr.
};

// Runs the program `args[0]` with the arguments `args` and waits for it.
// Can be called from several threads at the same time.
RunResult RunProcess(const std::vector<std::string>& args,
                     const RunOptions& options);

// Stops the process `pid` and its descendants (Python:
// codechecker_common.process.kill_process_tree()): SIGTERM to `pid`, then
// SIGKILL to the descendants still alive after 5 seconds.
void KillProcessTree(int pid);

// Sends `signal` to every process group started with
// RunOptions::new_session that is still running. Async-signal-safe.
void SignalRunningSessions(int signal);

// Returns the environment of the program as "KEY=VALUE" strings.
std::vector<std::string> CurrentEnvironment();

// Returns the value of the environment variable `name`, or nullopt.
std::optional<std::string> GetEnv(const std::string& name);

// Returns the value of `name` in the "KEY=VALUE" list `env`, or nullopt.
std::optional<std::string> GetEnv(const std::vector<std::string>& env,
                                  const std::string& name);

// Sets `name` to `value` in the "KEY=VALUE" list `env`.
void SetEnv(std::vector<std::string>* env, const std::string& name,
            const std::string& value);

// Returns the path of the program `command` like Python's
// shutil.which(command, path=path): `command` itself if it contains a
// slash, else the first executable file named `command` in the folders of
// `path` (PATH of the environment if nullopt). nullopt if there is none.
std::optional<std::string> Which(const std::string& command,
                                 const std::optional<std::string>& path);

// Returns the number of CPUs (Python: os.cpu_count()).
int CpuCount();

}  // namespace cc::platform

#endif  // CC_PLATFORM_SUBPROCESS_H_
