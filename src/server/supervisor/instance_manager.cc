#include "server/supervisor/instance_manager.h"

#include <yyjson.h>

#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "platform/locked_file.h"
#include "platform/process.h"
#include "platform/processes.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/unicode.h"

namespace cc::server::supervisor {
namespace {

constexpr util::Logger kLog("system");

// Returns the path of the file of the user's running servers.
std::string DescriptorPath() {
  return util::pypath::Join(
      {platform::HomeDirectory(), ".codechecker.instances.json"});
}

// Returns whether the process `pid` of this computer is a CodeChecker
// server of the user (Python: __check_instance()). Python servers run as
// ".../CodeChecker server ..." or ".../codechecker_common/cli.py server
// ...", C++ servers as ".../codechecker-cpp server ...".
bool IsRunningServer(const std::string& hostname, int pid) {
  if (hostname != platform::HostName()) return true;
  if (!platform::ProcessExists(pid)) return false;
  const std::optional<std::vector<std::string>> args =
      platform::ProcessCommandLine(pid);
  if (!args || args->empty()) return false;
  const bool python = args->size() > 1 &&
                      ((*args)[1].ends_with("codechecker_common/cli.py") ||
                       (*args)[1].ends_with("CodeChecker"));
  const bool cpp = (*args)[0].ends_with("codechecker-cpp");
  return (python || cpp) &&
         platform::ProcessUserName(pid) == platform::UserName();
}

// Returns the integer member `key` of `record`; 0 if it has none.
int IntMember(yyjson_val* record, const char* key) {
  return static_cast<int>(
      util::JsonToInt(util::JsonGet(record, key)).value_or(0));
}

// Rewrites the descriptor file: drops the records with the pid of
// `appended` or the "<host>:<pid>" `removed`, then adds `appended` (Python:
// __rewrite_instance_file()).
void RewriteInstanceFile(const std::optional<Instance>& appended,
                         const std::optional<std::string>& removed) {
  const std::string path = DescriptorPath();
  if (!util::pypath::Exists(path)) {
    if (!util::WriteFile(path, "[]").ok()) return;
    (void)platform::SetFileMode(path, 0600);
  }
  std::error_code error;
  std::optional<platform::LockedFile> file = platform::LockedFile::Open(
      path, platform::LockedFile::Lock::kExclusive, &error);
  if (!file) {
    kLog.Debug(error.message());
    return;
  }
  const std::optional<std::string> content = file->Read();
  Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(content.value_or("")));
  if (!document.ok()) {
    kLog.Warning(std::format("Failed to process json file: {}", path));
    kLog.Warning(document.error().message);
  }
  const util::MutableJsonDocument out = util::NewJsonDocument();
  yyjson_mut_val* records = yyjson_mut_arr(out.get());
  yyjson_mut_doc_set_root(out.get(), records);
  if (document.ok()) {
    yyjson_arr_iter iter =
        yyjson_arr_iter_with(yyjson_doc_get_root(document.value().get()));
    while (yyjson_val* record = yyjson_arr_iter_next(&iter)) {
      const int pid = IntMember(record, "pid");
      const std::string key = std::format(
          "{}:{}", util::JsonString(util::JsonGet(record, "hostname")), pid);
      if ((appended && pid == appended->pid) || (removed && key == *removed)) {
        continue;
      }
      yyjson_mut_arr_append(records, yyjson_val_mut_copy(out.get(), record));
    }
  }
  if (appended) {
    yyjson_mut_val* record = yyjson_mut_obj(out.get());
    yyjson_mut_obj_add_int(out.get(), record, "pid", appended->pid);
    yyjson_mut_obj_add_val(out.get(), record, "hostname",
                           util::MakeJsonString(out.get(), appended->hostname));
    yyjson_mut_obj_add_val(
        out.get(), record, "workspace",
        util::MakeJsonString(out.get(), appended->workspace));
    yyjson_mut_obj_add_int(out.get(), record, "port", appended->port);
    yyjson_mut_arr_append(records, record);
  }
  if (const std::error_code written =
          file->Replace(util::DumpJsonIndent(records, 2))) {
    kLog.Debug(written.message());
  }
}

}  // namespace

std::vector<Instance> GetInstances() {
  // Python: load_json(descriptor, {}, lock=True).
  const std::string path = DescriptorPath();
  std::error_code error;
  std::optional<platform::LockedFile> file = platform::LockedFile::Open(
      path, platform::LockedFile::Lock::kShared, &error);
  std::optional<std::string> content;
  if (file) content = file->Read();
  if (!content) {
    kLog.Warning(std::format("Failed to open json file: {}", path));
    kLog.Warning(util::OsError(error ? error.value() : 0, path).message);
    return {};
  }
  Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(*content));
  if (!document.ok()) {
    kLog.Warning(std::format("{} is not a valid json file.", path));
    kLog.Warning(document.error().message);
    return {};
  }
  std::vector<Instance> instances;
  yyjson_arr_iter iter =
      yyjson_arr_iter_with(yyjson_doc_get_root(document.value().get()));
  while (yyjson_val* record = yyjson_arr_iter_next(&iter)) {
    Instance instance = {
        .pid = IntMember(record, "pid"),
        .hostname = util::JsonString(util::JsonGet(record, "hostname")),
        .workspace = util::JsonString(util::JsonGet(record, "workspace")),
        .port = IntMember(record, "port")};
    if (IsRunningServer(instance.hostname, instance.pid)) {
      instances.push_back(std::move(instance));
    }
  }
  return instances;
}

void RegisterInstance(int pid, const std::string& workspace, int port) {
  RewriteInstanceFile(Instance{.pid = pid,
                               .hostname = platform::HostName(),
                               .workspace = workspace,
                               .port = port},
                      std::nullopt);
}

void UnregisterInstance(int pid) {
  RewriteInstanceFile(std::nullopt,
                      std::format("{}:{}", platform::HostName(), pid));
}

}  // namespace cc::server::supervisor
