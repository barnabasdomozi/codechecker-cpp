#include "platform/processes.h"

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <charconv>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "platform/process.h"

namespace cc::platform {
namespace {

// Returns the content of the file `path`; nullopt if it cannot be read.
std::optional<std::string> ReadProcFile(const std::string& path) {
  std::FILE* file = std::fopen(path.c_str(), "r");
  if (file == nullptr) return std::nullopt;
  std::string content;
  char buffer[4096];
  size_t size = 0;
  while ((size = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, size);
  }
  std::fclose(file);
  return content;
}

}  // namespace

int ForkChild(const std::function<int()>& body) {
  std::fflush(nullptr);
  const pid_t pid = fork();
  if (pid != 0) return pid;
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  for (const int signum : {SIGINT, SIGTERM, SIGHUP, SIGCHLD}) {
    sigaction(signum, &action, nullptr);
  }
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, nullptr);
  const int exit_code = body();
  std::fflush(nullptr);
  _exit(exit_code);
}

std::optional<int> ReapEndedChild() {
  int status = 0;
  const pid_t pid = waitpid(-1, &status, WNOHANG);
  if (pid <= 0) return std::nullopt;
  return pid;
}

bool ReapChild(int pid) {
  int status = 0;
  const pid_t result = waitpid(pid, &status, WNOHANG);
  // An error means that `pid` is not a child that is still running.
  return result != 0;
}

void WaitForChild(int pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
}

bool SendSignal(int pid, int signal) { return kill(pid, signal) == 0; }

bool ProcessExists(int pid) {
  return pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
}

std::optional<std::vector<std::string>> ProcessCommandLine(int pid) {
  const std::optional<std::string> content =
      ReadProcFile(std::format("/proc/{}/cmdline", pid));
  if (!content) return std::nullopt;
  std::vector<std::string> args;
  size_t start = 0;
  while (start < content->size()) {
    const size_t end = content->find('\0', start);
    if (end == std::string::npos) {
      args.push_back(content->substr(start));
      break;
    }
    args.push_back(content->substr(start, end - start));
    start = end + 1;
  }
  return args;
}

std::optional<std::string> ProcessUserName(int pid) {
  const std::optional<std::string> status =
      ReadProcFile(std::format("/proc/{}/status", pid));
  if (!status) return std::nullopt;
  // The line "Uid:\t<real>\t<effective>\t...": psutil uses the real uid.
  const size_t line = status->find("\nUid:");
  if (line == std::string::npos) return std::nullopt;
  const size_t digits = status->find_first_of("0123456789", line + 5);
  if (digits == std::string::npos) return std::nullopt;
  uid_t uid = 0;
  const std::from_chars_result parsed = std::from_chars(
      status->data() + digits, status->data() + status->size(), uid);
  if (parsed.ec != std::errc()) return std::nullopt;
  const std::optional<UserEntry> user = UserOfId(uid);
  return user ? user->name : std::to_string(uid);
}

}  // namespace cc::platform
