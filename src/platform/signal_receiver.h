// Signals received in a loop instead of in a signal handler.

#ifndef CC_PLATFORM_SIGNAL_RECEIVER_H_
#define CC_PLATFORM_SIGNAL_RECEIVER_H_

#include <chrono>
#include <initializer_list>
#include <optional>
#include <system_error>

namespace cc::platform {

// Blocks a set of signals in the calling thread and receives them through a
// signalfd, so a main loop can handle them one at a time with no
// async-signal-safety rules (the self-pipe of architecture.md §11.1).
// Create it before other threads start, so they inherit the blocked
// signals. Move-only; not thread-safe.
//
//   std::optional<SignalReceiver> signals =
//       SignalReceiver::Create({SIGINT, SIGTERM}, &error);
//   while (std::optional<int> signum = signals->Wait(timeout)) {
//     Handle(*signum);
//   }
class SignalReceiver {
 public:
  // Blocks `signals` and opens the signalfd. On failure, returns nullopt,
  // sets `*error` and leaves the blocked signals as they were.
  static std::optional<SignalReceiver> Create(
      std::initializer_list<int> signals, std::error_code* error);

  SignalReceiver(SignalReceiver&& other) noexcept;
  SignalReceiver& operator=(SignalReceiver&& other) noexcept;
  // Closes the signalfd; the signals stay blocked.
  ~SignalReceiver();

  // Returns the next signal, waiting at most `timeout` for it (forever if
  // nullopt); nullopt if none arrived.
  std::optional<int> Wait(std::optional<std::chrono::milliseconds> timeout);

 private:
  explicit SignalReceiver(int fd) : fd_(fd) {}

  int fd_;  // -1 after a move.
};

}  // namespace cc::platform

#endif  // CC_PLATFORM_SIGNAL_RECEIVER_H_
