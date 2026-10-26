// Options of the server's listening socket.

#ifndef CC_PLATFORM_SOCKET_OPTIONS_H_
#define CC_PLATFORM_SOCKET_OPTIONS_H_

#include <stdint.h>

namespace cc::platform {

// TCP keepalive settings; 0 leaves a setting at the system's default.
struct Keepalive {
  bool enabled = false;
  int64_t idle = 0;      // TCP_KEEPIDLE, seconds.
  int64_t interval = 0;  // TCP_KEEPINTVL, seconds.
  int64_t max_probe = 0;  // TCP_KEEPCNT.
};

// Sets the options of the listening socket `fd` that Python's HTTPServer
// and CodeChecker's configure_keepalive() set: SO_REUSEADDR, and TCP
// keepalive if `keepalive` is enabled.
void SetListeningSocketOptions(int fd, const Keepalive& keepalive);

}  // namespace cc::platform

#endif  // CC_PLATFORM_SOCKET_OPTIONS_H_
