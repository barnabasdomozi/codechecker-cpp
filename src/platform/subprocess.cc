#include "platform/subprocess.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace cc::platform {
namespace {

// The process groups started with RunOptions::new_session that are running;
// 0 marks a free slot. Read by SignalRunningSessions() in signal handlers,
// so it is a fixed array of atomics.
constexpr int kMaxSessions = 4096;
constinit std::atomic<int> running_sessions[kMaxSessions] = {};

void RegisterSession(int pid) {
  for (std::atomic<int>& slot : running_sessions) {
    int expected = 0;
    if (slot.compare_exchange_strong(expected, pid)) return;
  }
}

void UnregisterSession(int pid) {
  for (std::atomic<int>& slot : running_sessions) {
    int expected = pid;
    if (slot.compare_exchange_strong(expected, 0)) return;
  }
}

// A pipe whose ends are closed when they go out of scope; -1 for a closed
// end. Both ends are close-on-exec.
class Pipe {
 public:
  Pipe() {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == 0) {
      read_end_ = fds[0];
      write_end_ = fds[1];
    }
  }
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;
  ~Pipe() {
    CloseRead();
    CloseWrite();
  }

  int read_end() const { return read_end_; }
  int write_end() const { return write_end_; }
  void CloseRead() {
    if (read_end_ >= 0) close(read_end_);
    read_end_ = -1;
  }
  void CloseWrite() {
    if (write_end_ >= 0) close(write_end_);
    write_end_ = -1;
  }

 private:
  int read_end_ = -1;
  int write_end_ = -1;
};

// What the child writes to the error pipe when it cannot start the program.
struct StartFailure {
  int error;
  bool in_chdir;  // The working folder was wrong, not the program.
};

// The child side of RunProcess(): only async-signal-safe calls from here.
[[noreturn]] void RunChild(const std::vector<char*>& argv,
                           const std::vector<char*>& envp,
                           const std::vector<std::string>& candidates,
                           const RunOptions& options, int stdin_fd,
                           int stdout_fd, int stderr_fd, int error_fd) {
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  sigaction(SIGPIPE, &action, nullptr);
  if (options.new_session) setsid();
  if (stdin_fd >= 0) dup2(stdin_fd, STDIN_FILENO);
  if (stdout_fd >= 0) dup2(stdout_fd, STDOUT_FILENO);
  if (options.stderr_mode == Output::kStdout) {
    dup2(STDOUT_FILENO, STDERR_FILENO);
  } else if (stderr_fd >= 0) {
    dup2(stderr_fd, STDERR_FILENO);
  }
  // Like Python's close_fds=True: no other file of the parent stays open.
  close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);
  StartFailure failure = {.error = 0, .in_chdir = false};
  if (!options.cwd.empty() && chdir(options.cwd.c_str()) != 0) {
    failure = {.error = errno, .in_chdir = true};
  } else {
    int saved_error = 0;
    for (const std::string& candidate : candidates) {
      execve(candidate.c_str(), argv.data(), envp.data());
      if (errno != ENOENT && errno != ENOTDIR && saved_error == 0) {
        saved_error = errno;
      }
    }
    failure.error = saved_error != 0 ? saved_error : errno;
  }
  ssize_t written = write(error_fd, &failure, sizeof(failure));
  (void)written;
  _exit(255);
}

// Returns the paths where the program `program` is looked for, as Python's
// subprocess does: the program itself if it has a slash, else the folders of
// PATH in the child's environment.
std::vector<std::string> Candidates(const std::string& program,
                                    const RunOptions& options) {
  if (program.find('/') != std::string::npos) return {program};
  const std::optional<std::string> path =
      options.env ? GetEnv(*options.env, "PATH") : GetEnv("PATH");
  std::vector<std::string> candidates;
  const std::string folders = path.value_or("/bin:/usr/bin");
  size_t start = 0;
  while (start <= folders.size()) {
    size_t colon = folders.find(':', start);
    if (colon == std::string::npos) colon = folders.size();
    const std::string folder = folders.substr(start, colon - start);
    candidates.push_back(folder.empty() || folder.ends_with('/')
                             ? folder + program
                             : folder + "/" + program);
    start = colon + 1;
  }
  return candidates;
}

// Reads what is available on `fd` into `out`. Returns false at the end of
// the stream.
bool ReadAvailable(int fd, std::string* out) {
  char buffer[65536];
  while (true) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      out->append(buffer, static_cast<size_t>(count));
      return true;
    }
    if (count < 0 && errno == EINTR) continue;
    return count < 0 && errno == EAGAIN;
  }
}

int WaitForExit(int pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (WIFSIGNALED(status)) return -WTERMSIG(status);
  return WEXITSTATUS(status);
}

// Returns the parent of every process from /proc.
std::map<int, int> ParentProcesses() {
  std::map<int, int> parents;
  std::error_code error;
  for (std::filesystem::directory_iterator it("/proc", error), end;
       !error && it != end; it.increment(error)) {
    const std::string name = it->path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") !=
                            std::string::npos) {
      continue;
    }
    std::FILE* file = std::fopen((it->path() / "stat").c_str(), "r");
    if (file == nullptr) continue;
    char buffer[1024];
    const size_t size = std::fread(buffer, 1, sizeof(buffer), file);
    std::fclose(file);
    const std::string_view stat(buffer, size);
    // "<pid> (<name>) <state> <ppid> ...": the name may contain anything.
    const size_t close = stat.rfind(')');
    if (close == std::string_view::npos) continue;
    const size_t ppid_start = stat.find(' ', close + 2);
    if (ppid_start == std::string_view::npos) continue;
    int pid = 0;
    int ppid = 0;
    std::from_chars(name.data(), name.data() + name.size(), pid);
    std::from_chars(stat.data() + ppid_start + 1, stat.data() + stat.size(),
                    ppid);
    parents[pid] = ppid;
  }
  return parents;
}

}  // namespace

RunResult RunProcess(const std::vector<std::string>& args,
                     const RunOptions& options) {
  RunResult result;
  const std::vector<std::string> candidates = Candidates(args[0], options);
  std::vector<char*> argv;
  for (const std::string& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  const std::vector<std::string> environment =
      options.env ? *options.env : CurrentEnvironment();
  std::vector<char*> envp;
  for (const std::string& item : environment) {
    envp.push_back(const_cast<char*>(item.c_str()));
  }
  envp.push_back(nullptr);

  Pipe stdin_pipe;
  Pipe stdout_pipe;
  Pipe stderr_pipe;
  Pipe error_pipe;
  const int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
  const auto child_fd = [devnull](Output mode, const Pipe& pipe) {
    switch (mode) {
      case Output::kPipe:
        return pipe.write_end();
      case Output::kDevNull:
        return devnull;
      default:
        return -1;
    }
  };
  const int stdin_fd = options.input ? stdin_pipe.read_end() : -1;
  const int stdout_fd = child_fd(options.stdout_mode, stdout_pipe);
  const int stderr_fd = child_fd(options.stderr_mode, stderr_pipe);

  const pid_t pid = fork();
  if (pid == 0) {
    RunChild(argv, envp, candidates, options, stdin_fd, stdout_fd, stderr_fd,
             error_pipe.write_end());
  }
  if (devnull >= 0) close(devnull);
  stdin_pipe.CloseRead();
  stdout_pipe.CloseWrite();
  stderr_pipe.CloseWrite();
  error_pipe.CloseWrite();
  if (pid < 0) {
    result.start_error = errno;
    result.start_error_path = args[0];
    return result;
  }
  if (options.new_session) RegisterSession(pid);
  const auto start_time = std::chrono::steady_clock::now();

  StartFailure failure = {};
  ssize_t count = 0;
  while ((count = read(error_pipe.read_end(), &failure, sizeof(failure))) < 0 &&
         errno == EINTR) {
  }
  if (count == static_cast<ssize_t>(sizeof(failure))) {
    WaitForExit(pid);
    if (options.new_session) UnregisterSession(pid);
    result.start_error = failure.error;
    result.start_error_path = failure.in_chdir ? options.cwd : args[0];
    return result;
  }

  std::string input = options.input.value_or("");
  size_t written = 0;
  if (!options.input || input.empty()) stdin_pipe.CloseWrite();
  if (stdin_pipe.write_end() >= 0) {
    fcntl(stdin_pipe.write_end(), F_SETFL, O_NONBLOCK);
  }
  if (options.stdout_mode != Output::kPipe) stdout_pipe.CloseRead();
  if (options.stderr_mode != Output::kPipe) stderr_pipe.CloseRead();
  bool timed_out = false;
  bool exited = false;
  int return_code = 0;
  while (true) {
    std::vector<pollfd> fds;
    if (stdin_pipe.write_end() >= 0) {
      fds.push_back(
          {.fd = stdin_pipe.write_end(), .events = POLLOUT, .revents = 0});
    }
    if (stdout_pipe.read_end() >= 0) {
      fds.push_back(
          {.fd = stdout_pipe.read_end(), .events = POLLIN, .revents = 0});
    }
    if (stderr_pipe.read_end() >= 0) {
      fds.push_back(
          {.fd = stderr_pipe.read_end(), .events = POLLIN, .revents = 0});
    }
    int wait_ms = -1;
    if (options.timeout && !timed_out) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start_time);
      if (elapsed >= *options.timeout) {
        timed_out = true;
        if (options.on_timeout) options.on_timeout(pid);
        continue;
      }
      wait_ms = static_cast<int>((*options.timeout - elapsed).count());
    }
    if (fds.empty()) {
      // Nothing to read: wait for the child, waking up for the timeout.
      if (wait_ms < 0) break;
      int status = 0;
      const pid_t done = waitpid(pid, &status, WNOHANG);
      if (done == pid) {
        exited = true;
        return_code =
            WIFSIGNALED(status) ? -WTERMSIG(status) : WEXITSTATUS(status);
        break;
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(std::min(wait_ms, 10)));
      continue;
    }
    if (poll(fds.data(), fds.size(), wait_ms) < 0 && errno != EINTR) break;
    for (const pollfd& fd : fds) {
      if (fd.revents == 0) continue;
      if (fd.fd == stdin_pipe.write_end()) {
        const ssize_t sent = write(fd.fd, input.data() + written,
                                   input.size() - written);
        if (sent > 0) written += static_cast<size_t>(sent);
        if (sent < 0 && errno != EAGAIN && errno != EINTR) {
          written = input.size();
        }
        if (written == input.size()) stdin_pipe.CloseWrite();
      } else if (fd.fd == stdout_pipe.read_end()) {
        if (!ReadAvailable(fd.fd, &result.out)) stdout_pipe.CloseRead();
        if (options.on_stdout && !result.out.empty()) {
          options.on_stdout(result.out);
          result.out.clear();
        }
      } else if (!ReadAvailable(fd.fd, &result.err)) {
        stderr_pipe.CloseRead();
      }
    }
  }
  result.return_code = exited ? return_code : WaitForExit(pid);
  if (options.new_session) UnregisterSession(pid);
  return result;
}

void KillProcessTree(int pid) {
  const std::map<int, int> parents = ParentProcesses();
  std::set<int> descendants;
  std::vector<int> queue = {pid};
  while (!queue.empty()) {
    const int parent = queue.back();
    queue.pop_back();
    for (const auto& [child, child_parent] : parents) {
      if (child_parent == parent && descendants.insert(child).second) {
        queue.push_back(child);
      }
    }
  }
  kill(pid, SIGTERM);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!descendants.empty() && std::chrono::steady_clock::now() < deadline) {
    std::erase_if(descendants, [](int child) { return kill(child, 0) != 0; });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (const int child : descendants) kill(child, SIGKILL);
}

void SignalRunningSessions(int signal) {
  for (const std::atomic<int>& slot : running_sessions) {
    const int pid = slot.load();
    if (pid > 0) kill(-pid, signal);
  }
}

std::vector<std::string> CurrentEnvironment() {
  std::vector<std::string> env;
  for (char** item = environ; item != nullptr && *item != nullptr; ++item) {
    env.emplace_back(*item);
  }
  return env;
}

std::optional<std::string> GetEnv(const std::string& name) {
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) return std::nullopt;
  return std::string(value);
}

std::optional<std::string> GetEnv(const std::vector<std::string>& env,
                                  const std::string& name) {
  const std::string prefix = name + "=";
  for (const std::string& item : env) {
    if (item.starts_with(prefix)) return item.substr(prefix.size());
  }
  return std::nullopt;
}

void SetEnv(std::vector<std::string>* env, const std::string& name,
            const std::string& value) {
  const std::string prefix = name + "=";
  for (std::string& item : *env) {
    if (item.starts_with(prefix)) {
      item = prefix + value;
      return;
    }
  }
  env->push_back(prefix + value);
}

std::optional<std::string> Which(const std::string& command,
                                 const std::optional<std::string>& path) {
  const auto executable = [](const std::string& file) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::status(file, error);
    return !error && std::filesystem::exists(status) &&
           !std::filesystem::is_directory(status) &&
           access(file.c_str(), X_OK) == 0;
  };
  if (command.find('/') != std::string::npos) {
    if (executable(command)) return command;
    return std::nullopt;
  }
  std::string folders;
  if (path) {
    folders = *path;
  } else if (const std::optional<std::string> env_path = GetEnv("PATH")) {
    folders = *env_path;
  } else {
    char buffer[4096];
    const size_t size = confstr(_CS_PATH, buffer, sizeof(buffer));
    folders = size > 0 ? std::string(buffer) : "/bin:/usr/bin";
  }
  if (folders.empty()) return std::nullopt;
  std::set<std::string> seen;
  size_t start = 0;
  while (start <= folders.size()) {
    size_t colon = folders.find(':', start);
    if (colon == std::string::npos) colon = folders.size();
    const std::string folder = folders.substr(start, colon - start);
    start = colon + 1;
    if (!seen.insert(folder).second) continue;
    const std::string candidate =
        folder.empty() ? command
                       : (folder.ends_with('/') ? folder + command
                                                : folder + "/" + command);
    if (executable(candidate)) return candidate;
  }
  return std::nullopt;
}

int CpuCount() {
  const int64_t count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? static_cast<int>(count) : 1;
}

}  // namespace cc::platform
