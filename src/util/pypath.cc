#include "util/pypath.h"

#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "util/pyre.h"
#include "util/strings.h"

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

std::string Realpath(std::string_view path) {
  // The parts still to resolve, the next one at the back. nullopt marks the
  // end of a symbolic link's target; the link itself is below it.
  std::vector<std::optional<std::string>> rest;
  for (const std::string& part : Split(path, "/")) rest.emplace_back(part);
  std::ranges::reverse(rest);
  size_t part_count = rest.size();
  std::string resolved = path.starts_with('/') ? "/" : Abspath(".");
  // Links seen so far and their resolved paths; nullopt while a link is
  // being resolved, which detects loops.
  std::map<std::string, std::optional<std::string>> seen;
  while (part_count > 0) {
    std::optional<std::string> name = std::move(rest.back());
    rest.pop_back();
    if (!name) {
      seen[*rest.back()] = resolved;
      rest.pop_back();
      continue;
    }
    --part_count;
    if (name->empty() || *name == ".") continue;
    if (*name == "..") {
      resolved = resolved.substr(0, resolved.rfind('/'));
      if (resolved.empty()) resolved = "/";
      continue;
    }
    const std::string new_path =
        resolved == "/" ? resolved + *name : resolved + "/" + *name;
    struct stat info = {};
    if (lstat(new_path.c_str(), &info) != 0 || !S_ISLNK(info.st_mode)) {
      resolved = new_path;
      continue;
    }
    if (const auto known = seen.find(new_path); known != seen.end()) {
      // A link resolved earlier, or a loop: the path is kept as it is.
      resolved = known->second.value_or(new_path);
      continue;
    }
    std::error_code error;
    const std::string target =
        std::filesystem::read_symlink(new_path, error).string();
    if (error) {
      resolved = new_path;
      continue;
    }
    if (target.starts_with('/')) resolved = "/";
    seen[new_path] = std::nullopt;
    rest.emplace_back(new_path);
    rest.emplace_back(std::nullopt);
    std::vector<std::string> target_parts = Split(target, "/");
    part_count += target_parts.size();
    for (auto it = target_parts.rbegin(); it != target_parts.rend(); ++it) {
      rest.emplace_back(std::move(*it));
    }
  }
  return resolved;
}

std::pair<std::string, std::string> Splitext(std::string_view path) {
  const size_t slash = path.rfind('/');
  const size_t dot = path.rfind('.');
  const size_t name_start = slash == std::string_view::npos ? 0 : slash + 1;
  if (dot == std::string_view::npos || dot < name_start) {
    return {std::string(path), ""};
  }
  // Leading dots of the file name do not start an extension.
  const size_t first = path.find_first_not_of('.', name_start);
  if (first == std::string_view::npos || dot < first) {
    return {std::string(path), ""};
  }
  return {std::string(path.substr(0, dot)), std::string(path.substr(dot))};
}

std::optional<std::string> Commonpath(const std::vector<std::string>& paths) {
  const bool absolute = paths.front().starts_with('/');
  std::vector<std::vector<std::string>> split_paths;
  for (const std::string& path : paths) {
    if (path.starts_with('/') != absolute) return std::nullopt;
    std::vector<std::string> parts;
    for (std::string& part : Split(path, "/")) {
      if (!part.empty() && part != ".") parts.push_back(std::move(part));
    }
    split_paths.push_back(std::move(parts));
  }
  const auto [first, last] = std::ranges::minmax(split_paths);
  std::vector<std::string> common = first;
  for (size_t i = 0; i < first.size(); ++i) {
    if (first[i] != last[i]) {
      common.resize(i);
      break;
    }
  }
  return (absolute ? "/" : "") + util::Join(common, "/");
}

std::string Relpath(std::string_view path, std::string_view start) {
  const auto parts = [](std::string_view target) {
    std::vector<std::string> result = Split(Abspath(target), "/");
    std::erase(result, "");
    return result;
  };
  const std::vector<std::string> start_parts = parts(start);
  const std::vector<std::string> path_parts = parts(path);
  const size_t common = static_cast<size_t>(
      std::ranges::mismatch(start_parts, path_parts).in1 -
      start_parts.begin());
  std::vector<std::string> relative(start_parts.size() - common, "..");
  relative.insert(relative.end(), path_parts.begin() + common,
                  path_parts.end());
  return relative.empty() ? "." : util::Join(relative, "/");
}

std::string Expandvars(std::string_view path) {
  // Python: re.compile(r'\$(\w+|\{[^}]*\})', re.ASCII).
  static const Regex& kVariable =
      *new Regex(R"(\$([A-Za-z0-9_]+|\{[^}]*\}))");
  std::string result;
  size_t pos = 0;
  for (const RegexMatch& match : kVariable.FindAll(path)) {
    std::string_view name = *match.Group(1);
    if (name.starts_with('{')) name = name.substr(1, name.size() - 2);
    // An unset variable is kept as it is.
    const char* value = std::getenv(std::string(name).c_str());
    if (value == nullptr) continue;
    result.append(path.substr(pos, match.Start() - pos));
    result += value;
    pos = match.End();
  }
  result.append(path.substr(pos));
  return result;
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

bool IsLink(const std::string& path) {
  std::error_code error;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(path, error);
  return !error && std::filesystem::is_symlink(status);
}

}  // namespace cc::util::pypath
