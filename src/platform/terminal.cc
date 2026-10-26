#include "platform/terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <optional>
#include <string>

namespace cc::platform {

std::optional<int> StdoutTerminalColumns() {
  struct winsize size = {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return std::nullopt;
  return int{size.ws_col};
}

std::optional<std::string> ReadStdinLine() {
  std::string line;
  int c = 0;
  while ((c = std::fgetc(stdin)) != EOF) {
    if (c == '\n') return line;
    line.push_back(static_cast<char>(c));
  }
  if (line.empty()) return std::nullopt;
  return line;
}

}  // namespace cc::platform
