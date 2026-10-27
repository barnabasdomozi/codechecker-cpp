#include "platform/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cc::platform {
namespace {

// The largest message Receive() reads; Send() is limited by the socket
// buffer, which is smaller.
constexpr size_t kMaxMessageSize = 1 << 20;

// Returns the error of the last failed system call.
std::error_code LastError() {
  return std::error_code(errno, std::system_category());
}

}  // namespace

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) close(fd_);
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

FileDescriptor::~FileDescriptor() {
  if (fd_ >= 0) close(fd_);
}

std::optional<Pipe> MakePipe(std::error_code* error) {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) {
    *error = LastError();
    return std::nullopt;
  }
  return Pipe{.read_end = FileDescriptor(fds[0]),
              .write_end = FileDescriptor(fds[1])};
}

void WaitUntilClosed(const FileDescriptor& read_end) {
  char buffer[64];
  while (true) {
    const ssize_t count = read(read_end.get(), buffer, sizeof(buffer));
    if (count == 0) return;
    if (count < 0 && errno != EINTR) return;
  }
}

std::optional<MessageQueue> MessageQueue::Create(std::error_code* error) {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) != 0) {
    *error = LastError();
    return std::nullopt;
  }
  return MessageQueue(fds[0], fds[1]);
}

std::error_code MessageQueue::Send(std::string_view text, int fd) {
  iovec data = {.iov_base = const_cast<char*>(text.data()),
                .iov_len = text.size()};
  msghdr message = {};
  message.msg_iov = &data;
  message.msg_iovlen = 1;
  // The ancillary data that carries the descriptor, aligned as cmsghdr
  // requires.
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
  if (fd >= 0) {
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(header), &fd, sizeof(fd));
  }
  while (sendmsg(send_fd_.get(), &message, MSG_NOSIGNAL) < 0) {
    if (errno != EINTR) return LastError();
  }
  return std::error_code();
}

std::optional<MessageQueue::Message> MessageQueue::Receive(
    std::chrono::milliseconds timeout) {
  pollfd ready = {.fd = receive_fd_.get(), .events = POLLIN, .revents = 0};
  if (poll(&ready, 1, static_cast<int>(timeout.count())) <= 0) {
    return std::nullopt;
  }
  std::string text(kMaxMessageSize, '\0');
  iovec data = {.iov_base = text.data(), .iov_len = text.size()};
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
  msghdr message = {};
  message.msg_iov = &data;
  message.msg_iovlen = 1;
  message.msg_control = control;
  message.msg_controllen = sizeof(control);
  // Several receivers wake up for one message; the others must not block.
  const ssize_t count =
      recvmsg(receive_fd_.get(), &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
  if (count < 0) return std::nullopt;
  text.resize(static_cast<size_t>(count));
  Message received = {.text = std::move(text), .fd = FileDescriptor()};
  for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr;
       header = CMSG_NXTHDR(&message, header)) {
    if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
      int fd = -1;
      std::memcpy(&fd, CMSG_DATA(header), sizeof(fd));
      received.fd = FileDescriptor(fd);
    }
  }
  return received;
}

}  // namespace cc::platform
