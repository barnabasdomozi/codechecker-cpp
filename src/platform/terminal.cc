#include "platform/terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <optional>

namespace cc::platform {

std::optional<int> StdoutTerminalColumns() {
  struct winsize size = {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return std::nullopt;
  return int{size.ws_col};
}

}  // namespace cc::platform
