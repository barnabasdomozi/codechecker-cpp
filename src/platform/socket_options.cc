#include "platform/socket_options.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <sys/socket.h>

namespace cc::platform {
namespace {

void SetOption(int fd, int level, int name, int64_t value) {
  const int option = static_cast<int>(value);
  setsockopt(fd, level, name, &option, sizeof(option));
}

}  // namespace

void SetListeningSocketOptions(int fd, const Keepalive& keepalive) {
  SetOption(fd, SOL_SOCKET, SO_REUSEADDR, 1);
  if (!keepalive.enabled) return;
  SetOption(fd, SOL_SOCKET, SO_KEEPALIVE, 1);
  if (keepalive.idle != 0) {
    SetOption(fd, IPPROTO_TCP, TCP_KEEPIDLE, keepalive.idle);
  }
  if (keepalive.interval != 0) {
    SetOption(fd, IPPROTO_TCP, TCP_KEEPINTVL, keepalive.interval);
  }
  if (keepalive.max_probe != 0) {
    SetOption(fd, IPPROTO_TCP, TCP_KEEPCNT, keepalive.max_probe);
  }
}

}  // namespace cc::platform
