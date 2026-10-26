#include "platform/process.h"

#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

#include "platform/subprocess.h"

namespace cc::platform {
namespace {

// The child process of RunForked(), for the signal handler; 0 if none.
constinit std::atomic<int> forked_child = 0;

void ExitWithSignalCode(int signum) {
  SignalRunningSessions(SIGTERM);
  _exit(128 + signum);
}

void ForwardSignal(int signum) {
  const int child = forked_child.load();
  if (child > 0) kill(child, signum);
}

void PrintStackTraceAndExit() {
  std::fflush(nullptr);
  constexpr char kHeader[] = "Internal error. Stack trace:\n";
  if (write(STDOUT_FILENO, kHeader, sizeof(kHeader) - 1) < 0) {
    _exit(1);
  }
  void* frames[128];
  const int count = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
  backtrace_symbols_fd(frames, count, STDOUT_FILENO);
  _exit(1);
}

}  // namespace

std::string ExecutablePath() {
  std::error_code error;
  std::filesystem::path path =
      std::filesystem::read_symlink("/proc/self/exe", error);
  return error ? std::string() : path.string();
}

int ProcessId() { return getpid(); }

uint64_t ThreadId() {
  // pthread_t is an integer on Linux; Python's get_ident() prints it.
  return static_cast<uint64_t>(pthread_self());
}

void ExitOnTerminationSignals() {
  struct sigaction action = {};
  action.sa_handler = ExitWithSignalCode;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
}

void IgnoreBrokenPipes() {
  struct sigaction action = {};
  action.sa_handler = SIG_IGN;
  sigemptyset(&action.sa_mask);
  sigaction(SIGPIPE, &action, nullptr);
}

int RunForked(const std::function<int()>& child) {
  const pid_t pid = fork();
  if (pid == 0) std::exit(child());
  if (pid < 0) return child();
  forked_child.store(pid);
  struct sigaction action = {};
  action.sa_handler = ForwardSignal;
  sigemptyset(&action.sa_mask);
  for (const int signum : {SIGINT, SIGTERM, SIGHUP, SIGCHLD}) {
    sigaction(signum, &action, nullptr);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  forked_child.store(0);
  if (WIFSIGNALED(status)) return -WTERMSIG(status);
  return WEXITSTATUS(status);
}

void InstallTerminateHandler() { std::set_terminate(PrintStackTraceAndExit); }

}  // namespace cc::platform
