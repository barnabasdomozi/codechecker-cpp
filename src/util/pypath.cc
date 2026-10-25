#include "util/pypath.h"

#include <stddef.h>

#include <initializer_list>
#include <string>
#include <string_view>

namespace cc::util::pypath {

std::string Dirname(std::string_view path) {
  const size_t end = path.rfind('/') + 1;  // 0 if there is no slash.
  std::string_view head = path.substr(0, end);
  if (head.find_first_not_of('/') != std::string_view::npos) {
    head.remove_suffix(head.size() - head.find_last_not_of('/') - 1);
  }
  return std::string(head);
}

std::string Join(std::initializer_list<std::string_view> parts) {
  std::string path;
  bool first = true;
  for (const std::string_view part : parts) {
    if (first || part.starts_with('/')) {
      path = part;
    } else if (path.empty() || path.ends_with('/')) {
      path += part;
    } else {
      path += '/';
      path += part;
    }
    first = false;
  }
  return path;
}

}  // namespace cc::util::pypath
