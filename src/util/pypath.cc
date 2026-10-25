#include "util/pypath.h"

#include <stddef.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace cc::util::pypath {
namespace {

std::filesystem::file_status Status(const std::string& path) {
  std::error_code error;
  return std::filesystem::status(path, error);
}

}  // namespace

std::string Basename(std::string_view path) {
  return std::string(path.substr(path.rfind('/') + 1));
}

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

std::string Normpath(std::string_view path) {
  if (path.empty()) return ".";
  size_t initial_slashes = path.starts_with('/') ? 1 : 0;
  if (path.starts_with("//") && !path.starts_with("///")) initial_slashes = 2;
  std::vector<std::string_view> components;
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t slash = path.find('/', pos);
    if (slash == std::string_view::npos) slash = path.size();
    const std::string_view component = path.substr(pos, slash - pos);
    pos = slash + 1;
    if (component.empty() || component == ".") continue;
    if (component != ".." || (initial_slashes == 0 && components.empty()) ||
        (!components.empty() && components.back() == "..")) {
      components.push_back(component);
    } else if (!components.empty()) {
      components.pop_back();
    }
  }
  std::string result(initial_slashes, '/');
  for (size_t i = 0; i < components.size(); ++i) {
    if (i > 0) result += '/';
    result += components[i];
  }
  return result.empty() ? "." : result;
}

std::string Abspath(std::string_view path) {
  if (path.starts_with('/')) return Normpath(path);
  std::error_code error;
  const std::string cwd = std::filesystem::current_path(error).string();
  return Normpath(Join({cwd, path}));
}

bool Exists(const std::string& path) {
  return std::filesystem::exists(Status(path));
}

bool IsFile(const std::string& path) {
  return std::filesystem::is_regular_file(Status(path));
}

bool IsDir(const std::string& path) {
  return std::filesystem::is_directory(Status(path));
}

}  // namespace cc::util::pypath
