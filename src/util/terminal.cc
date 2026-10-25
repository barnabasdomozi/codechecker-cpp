#include "util/terminal.h"

#include <stdint.h>

#include <cstdlib>
#include <limits>
#include <optional>

#include "platform/terminal.h"
#include "util/strings.h"

namespace cc::util {

std::optional<int> ColumnsVariable() {
  const char* value = std::getenv("COLUMNS");
  if (value == nullptr) return std::nullopt;
  const std::optional<int64_t> columns = ParseInt(value);
  if (!columns || *columns > std::numeric_limits<int>::max() ||
      *columns < std::numeric_limits<int>::min()) {
    return std::nullopt;
  }
  return static_cast<int>(*columns);
}

int TerminalColumns() {
  const std::optional<int> variable = ColumnsVariable();
  if (variable && *variable > 0) return *variable;
  const int columns = platform::StdoutTerminalColumns().value_or(0);
  return columns > 0 ? columns : 80;
}

}  // namespace cc::util
