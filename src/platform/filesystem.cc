#include "platform/filesystem.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace cc::platform {

std::vector<std::string> ListDirectory(const std::string& path) {
  std::vector<std::string> names;
  std::error_code error;
  for (std::filesystem::directory_iterator it(path, error), end;
       !error && it != end; it.increment(error)) {
    names.push_back(it->path().filename().string());
  }
  return names;
}

std::vector<std::string> WalkFiles(const std::string& path) {
  std::vector<std::string> files;
  std::vector<std::string> folders;
  std::error_code error;
  for (std::filesystem::directory_iterator it(path, error), end;
       !error && it != end; it.increment(error)) {
    // os.path.join(path, name).
    const std::string name = it->path().filename().string();
    const std::string joined =
        path.empty() || path.ends_with('/') ? path + name : path + "/" + name;
    std::error_code status_error;
    if (!it->is_directory(status_error)) {
      files.push_back(joined);
    } else if (!it->is_symlink(status_error)) {
      folders.push_back(joined);
    }
  }
  for (const std::string& folder : folders) {
    std::vector<std::string> inner = WalkFiles(folder);
    files.insert(files.end(), inner.begin(), inner.end());
  }
  return files;
}

std::error_code MakeDirs(const std::string& path) {
  std::error_code error;
  if (std::filesystem::exists(std::filesystem::symlink_status(path, error))) {
    return std::make_error_code(std::errc::file_exists);
  }
  std::filesystem::create_directories(path, error);
  return error;
}

std::error_code RemoveTree(const std::string& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  return error;
}

}  // namespace cc::platform
