#include "platform/filesystem.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "platform/subprocess.h"

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

std::vector<WalkedFolder> Walk(const std::string& path) {
  std::error_code error;
  std::filesystem::directory_iterator it(path, error);
  if (error) return {};
  std::vector<WalkedFolder> folders = {{.path = path, .files = {}}};
  std::vector<std::string> subfolders;
  for (const std::filesystem::directory_iterator end; !error && it != end;
       it.increment(error)) {
    // os.path.join(path, name).
    const std::string name = it->path().filename().string();
    const std::string joined =
        path.empty() || path.ends_with('/') ? path + name : path + "/" + name;
    std::error_code status_error;
    if (!it->is_directory(status_error)) {
      folders.front().files.push_back(joined);
    } else if (!it->is_symlink(status_error)) {
      subfolders.push_back(joined);
    }
  }
  for (const std::string& subfolder : subfolders) {
    std::vector<WalkedFolder> inner = Walk(subfolder);
    folders.insert(folders.end(), std::make_move_iterator(inner.begin()),
                   std::make_move_iterator(inner.end()));
  }
  return folders;
}

std::vector<std::string> WalkFiles(const std::string& path) {
  std::vector<std::string> files;
  for (const WalkedFolder& folder : Walk(path)) {
    files.insert(files.end(), folder.files.begin(), folder.files.end());
  }
  return files;
}

std::optional<double> ModificationTime(const std::string& path) {
  struct stat status = {};
  if (stat(path.c_str(), &status) != 0) return std::nullopt;
  // Python computes st_mtime in the same way.
  return static_cast<double>(status.st_mtim.tv_sec) +
         static_cast<double>(status.st_mtim.tv_nsec) * 1.0e-9;
}

std::error_code MakeDirs(const std::string& path) {
  std::error_code error;
  if (std::filesystem::exists(std::filesystem::symlink_status(path, error))) {
    return std::make_error_code(std::errc::file_exists);
  }
  return EnsureDirs(path);
}

std::error_code EnsureDirs(const std::string& path) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  return error;
}

std::error_code CopyFile(const std::string& source,
                         const std::string& destination) {
  std::error_code error;
  std::filesystem::copy_file(source, destination,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  return error;
}

std::error_code RemoveTree(const std::string& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  return error;
}

std::string TempDir() {
  std::vector<std::string> candidates;
  for (const char* name : {"TMPDIR", "TEMP", "TMP"}) {
    std::optional<std::string> value = GetEnv(name);
    if (value && !value->empty()) candidates.push_back(std::move(*value));
  }
  candidates.insert(candidates.end(), {"/tmp", "/var/tmp", "/usr/tmp"});
  for (const std::string& candidate : candidates) {
    std::error_code error;
    const bool is_dir = std::filesystem::is_directory(candidate, error);
    if (!error && is_dir && access(candidate.c_str(), W_OK | X_OK) == 0) {
      return candidate;
    }
  }
  // Python falls back to os.curdir if os.getcwd() fails.
  std::error_code error;
  const std::filesystem::path cwd = std::filesystem::current_path(error);
  return error ? "." : cwd.string();
}

std::optional<std::string> MakeTempDir() {
  std::string path = TempDir() + "/tmpXXXXXX";
  if (mkdtemp(path.data()) == nullptr) return std::nullopt;
  return path;
}

std::optional<std::string> MakeTempFile(const std::string& dir,
                                        const std::string& suffix) {
  std::string path = dir + "/tmpXXXXXX" + suffix;
  const int fd = mkstemps(path.data(), static_cast<int>(suffix.size()));
  if (fd < 0) return std::nullopt;
  close(fd);
  return path;
}

}  // namespace cc::platform
