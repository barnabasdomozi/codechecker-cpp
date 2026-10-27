// Communication between the processes of the server: the task queue that
// the API processes and the task workers share, and the pipe that tells an
// API process that a task has ended (architecture.md §11.1).

#ifndef CC_PLATFORM_IPC_H_
#define CC_PLATFORM_IPC_H_

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace cc::platform {

// An owned file descriptor, closed when the object is destroyed. Move-only.
class FileDescriptor {
 public:
  FileDescriptor() = default;
  // Takes ownership of `fd`; -1 owns nothing.
  explicit FileDescriptor(int fd) : fd_(fd) {}

  FileDescriptor(FileDescriptor&& other) noexcept;
  FileDescriptor& operator=(FileDescriptor&& other) noexcept;
  ~FileDescriptor();

  // Returns the descriptor; -1 if the object owns none.
  int get() const { return fd_; }

 private:
  int fd_ = -1;
};

// The two ends of a pipe.
struct Pipe {
  FileDescriptor read_end;
  FileDescriptor write_end;
};

// Creates a pipe whose ends are closed on exec(); nullopt on failure, with
// the reason in `*error`.
std::optional<Pipe> MakePipe(std::error_code* error);

// Waits until every copy of the write end of the pipe of `read_end` is
// closed, in this and in other processes. What is written is dropped.
void WaitUntilClosed(const FileDescriptor& read_end);

// A queue of messages between the processes forked from the process that
// created it: a socketpair(AF_UNIX, SOCK_SEQPACKET) the processes inherit.
// Any number of processes may send and receive; each message reaches one
// receiver, whole. A message may carry a file descriptor (SCM_RIGHTS).
// Move-only; one thread of a process uses it at a time.
class MessageQueue {
 public:
  // A received message.
  struct Message {
    std::string text;
    FileDescriptor fd;  // The descriptor sent with it; -1 if none.
  };

  // Creates the queue; nullopt on failure, with the reason in `*error`.
  static std::optional<MessageQueue> Create(std::error_code* error);

  MessageQueue(MessageQueue&& other) = default;
  MessageQueue& operator=(MessageQueue&& other) = default;

  // Sends `text`, which must be shorter than the socket buffer (some
  // hundred kilobytes), with a copy of the descriptor `fd` unless it is
  // -1. Waits while the queue is full.
  std::error_code Send(std::string_view text, int fd);

  // Returns the next message, waiting at most `timeout` for it; nullopt if
  // none arrived (also when another receiver took it first).
  std::optional<Message> Receive(std::chrono::milliseconds timeout);

 private:
  MessageQueue(int send_fd, int receive_fd)
      : send_fd_(send_fd), receive_fd_(receive_fd) {}

  FileDescriptor send_fd_;
  FileDescriptor receive_fd_;
};

}  // namespace cc::platform

#endif  // CC_PLATFORM_IPC_H_
