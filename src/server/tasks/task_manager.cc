#include "server/tasks/task_manager.h"

#include <stdint.h>

#include <format>
#include <string>
#include <string_view>
#include <system_error>

#include "platform/filesystem.h"
#include "server/database/connection.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::server::tasks {
namespace {

constexpr util::Logger kLog("server");

// Returns `comments` with `comment` appended as a comment by `actor`
// (Python: BackgroundTask.add_comment()).
std::string AddComment(const std::string& comments, std::string_view comment,
                       std::string_view actor) {
  std::string result = comments;
  if (!result.empty()) result += "\n----------\n";
  // Python: str(datetime.now(timezone.utc)).
  result += std::format("{} at {}+00:00:\n{}", actor,
                        util::FormatPythonTime(util::UtcNow()), comment);
  return result;
}

}  // namespace

std::string TaskDataDirectory(const std::string& workspace,
                              std::string_view machine_id) {
  // Python: CHARS_INVALID_IN_PATH.
  constexpr std::string_view kInvalid = "'\"<>:\\/|*?. ";
  std::string name(machine_id);
  for (char& c : name) {
    if (kInvalid.find(c) != std::string_view::npos) c = '_';
  }
  return util::pypath::Join({workspace, "codechecker_tasks", name});
}

void DestroyAllTemporaryData(const std::string& workspace,
                             std::string_view machine_id) {
  const std::string directory = TaskDataDirectory(workspace, machine_id);
  if (const std::error_code error = platform::RemoveTree(directory)) {
    kLog.Warning(std::format(
        "Failed to remove background tasks' data_dirs at '{}':\n{}",
        directory, error.message()));
  }
  // Python's TaskManager creates the folder when it is constructed.
  (void)platform::EnsureDirs(directory);
}

Result<int64_t> DropAllIncompleteTasks(db::Connection& config_db,
                                       std::string_view machine_id,
                                       std::string_view action) {
  Result<db::QueryResult> tasks = config_db.Query(
      "SELECT token, comments FROM background_tasks WHERE machine_id = ? AND "
      "status IN ('allocated', 'enqueued', 'running')",
      {std::string(machine_id)});
  if (!tasks.ok()) return Result<int64_t>(tasks.error());
  const std::string finished_at = util::FormatDatabaseTime(util::UtcNow());
  for (const db::Row& task : tasks.value().rows) {
    Result<db::QueryResult> dropped = config_db.Query(
        "UPDATE background_tasks SET status = 'dropped', finished_at = ?, "
        "comments = ? WHERE token = ?",
        {finished_at,
         AddComment(task.Text(1), std::format("DROPPED!\n{}", action),
                    "SYSTEM"),
         task.Text(0)});
    if (!dropped.ok()) return Result<int64_t>(dropped.error());
  }
  return Result<int64_t>(static_cast<int64_t>(tasks.value().rows.size()));
}

}  // namespace cc::server::tasks
