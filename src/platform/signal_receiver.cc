#include "platform/signal_receiver.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <chrono>
#include <initializer_list>
#include <optional>
#include <system_error>

namespace cc::platform {

std::optional<SignalReceiver> SignalReceiver::Create(
    std::initializer_list<int> signals, std::error_code* error) {
  sigset_t set;
  sigemptyset(&set);
  for (const int signum : signals) sigaddset(&set, signum);
  // The signals are blocked first, so none is handled by its old
  // disposition once the signalfd exists.
  sigset_t old_set;
  pthread_sigmask(SIG_BLOCK, &set, &old_set);
  const int fd = signalfd(-1, &set, SFD_CLOEXEC);
  if (fd < 0) {
    *error = std::error_code(errno, std::generic_category());
    pthread_sigmask(SIG_SETMASK, &old_set, nullptr);
    return std::nullopt;
  }
  return SignalReceiver(fd);
}

SignalReceiver::SignalReceiver(SignalReceiver&& other) noexcept
    : fd_(other.fd_) {
  other.fd_ = -1;
}

SignalReceiver& SignalReceiver::operator=(SignalReceiver&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) close(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

SignalReceiver::~SignalReceiver() {
  if (fd_ >= 0) close(fd_);
}

std::optional<int> SignalReceiver::Wait(
    std::optional<std::chrono::milliseconds> timeout) {
  struct pollfd poll_fd = {.fd = fd_, .events = POLLIN, .revents = 0};
  const int timeout_ms = timeout ? static_cast<int>(timeout->count()) : -1;
  int ready = 0;
  while ((ready = poll(&poll_fd, 1, timeout_ms)) < 0 && errno == EINTR) {
  }
  if (ready <= 0) return std::nullopt;
  struct signalfd_siginfo info = {};
  if (read(fd_, &info, sizeof(info)) != sizeof(info)) return std::nullopt;
  return static_cast<int>(info.ssi_signo);
}

}  // namespace cc::platform
