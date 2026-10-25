#include "platform/process.h"

#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace cc::platform {
namespace {

void ExitWithSignalCode(int signum) { _exit(128 + signum); }

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

void InstallTerminateHandler() { std::set_terminate(PrintStackTraceAndExit); }

}  // namespace cc::platform
