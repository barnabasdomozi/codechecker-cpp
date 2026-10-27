#include "server/tasks/task_record.h"

#include <stdint.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "server/database/connection.h"
#include "server/database/sql.h"
#include "util/datetime.h"
#include "util/result.h"

namespace cc::server::tasks {
namespace {

// The columns of background_tasks in the order RecordOfRow() reads them.
constexpr std::string_view kColumns =
    "token, machine_id, kind, status, product_id, username, summary, "
    "comments, enqueued_at, started_at, finished_at, last_seen_at, consumed, "
    "cancel_flag";

// Returns the time in `column` of `row`; nullopt for NULL.
std::optional<int64_t> TimeOf(const db::Row& row, int column) {
  if (row.IsNull(column)) return std::nullopt;
  return util::ParseDatabaseTime(row.Text(column));
}

// Returns the task in the kColumns of `row`.
TaskRecord RecordOfRow(const db::Row& row) {
  return TaskRecord{.token = row.Text(0),
                    .machine_id = row.OptionalText(1),
                    .kind = row.Text(2),
                    .status = row.Text(3),
                    .product_id = row.OptionalInt(4),
                    .username = row.OptionalText(5),
                    .summary = row.Text(6),
                    .comments = row.OptionalText(7),
                    .enqueued_at = TimeOf(row, 8),
                    .started_at = TimeOf(row, 9),
                    .finished_at = TimeOf(row, 10),
                    .last_seen_at = TimeOf(row, 11),
                    .consumed = row.Bool(12),
                    .cancel_flag = row.Bool(13)};
}

// Moves `task` from the status `from` to `to`, setting `*time` to now.
Result<void> Transition(std::string_view from, std::string_view to,
                        std::optional<int64_t>* time, TaskRecord* task) {
  if (task->status != from) {
    return Result<void>(Error{.message = std::format(
                                  "Invalid transition '{}' -> '{}'",
                                  task->status, to)});
  }
  task->status = to;
  *time = util::UtcNow();
  return Result<void>();
}

}  // namespace

TaskRecord NewTaskRecord(std::string token, std::string kind,
                         std::string summary, std::string machine_id,
                         std::optional<std::string> username,
                         std::optional<int64_t> product_id) {
  return TaskRecord{.token = std::move(token),
                    .machine_id = std::move(machine_id),
                    .kind = std::move(kind),
                    .status = "allocated",
                    .product_id = product_id,
                    .username = std::move(username),
                    .summary = std::move(summary),
                    .last_seen_at = util::UtcNow()};
}

void AddComment(std::string_view comment,
                const std::optional<std::string>& actor, TaskRecord* task) {
  std::string& comments = task->comments.emplace(task->comments.value_or(""));
  if (!comments.empty()) comments += "\n----------\n";
  // Python: str(datetime.now(timezone.utc)).
  comments += std::format("{} at {}+00:00:\n{}", actor.value_or("<unknown>"),
                          util::FormatPythonTime(util::UtcNow()), comment);
}

void Heartbeat(TaskRecord* task) {
  if (task->status == "enqueued" || task->status == "running") {
    task->last_seen_at = util::UtcNow();
  }
}

Result<void> SetEnqueued(TaskRecord* task) {
  return Transition("allocated", "enqueued", &task->enqueued_at, task);
}

Result<void> SetRunning(TaskRecord* task) {
  return Transition("enqueued", "running", &task->started_at, task);
}

Result<void> SetFinished(bool successfully, TaskRecord* task) {
  return Transition("running", successfully ? "completed" : "failed",
                    &task->finished_at, task);
}

void SetAbandoned(bool force_dropped, TaskRecord* task) {
  task->status = !force_dropped && task->cancel_flag ? "cancelled" : "dropped";
  task->finished_at = util::UtcNow();
}

bool IsTerminated(const TaskRecord& task) {
  return task.status != "allocated" && task.status != "enqueued" &&
         task.status != "running";
}

bool CanBeCancelled(const TaskRecord& task) {
  return !IsTerminated(task) && !task.cancel_flag;
}

Result<void> InsertTaskRecord(db::Connection& config_db,
                              const TaskRecord& task) {
  const Result<db::QueryResult> inserted = config_db.Query(
      std::format("INSERT INTO background_tasks ({}) VALUES (?, ?, ?, ?, ?, "
                  "?, ?, ?, ?, ?, ?, ?, ?, ?)",
                  kColumns),
      {task.token, db::OptionalText(task.machine_id), task.kind, task.status,
       task.product_id ? db::Param(*task.product_id) : db::Param(nullptr),
       db::OptionalText(task.username), task.summary,
       db::OptionalText(task.comments), db::OptionalTime(task.enqueued_at),
       db::OptionalTime(task.started_at),
       db::OptionalTime(task.finished_at), db::OptionalTime(task.last_seen_at),
       task.consumed, task.cancel_flag});
  return inserted.ok() ? Result<void>() : Result<void>(inserted.error());
}

Result<void> UpdateTaskRecord(db::Connection& config_db,
                              const TaskRecord& task) {
  const Result<db::QueryResult> updated = config_db.Query(
      "UPDATE background_tasks SET status = ?, comments = ?, "
      "enqueued_at = ?, started_at = ?, finished_at = ?, last_seen_at = ?, "
      "consumed = ?, cancel_flag = ? WHERE token = ?",
      {task.status, db::OptionalText(task.comments),
       db::OptionalTime(task.enqueued_at), db::OptionalTime(task.started_at),
       db::OptionalTime(task.finished_at), db::OptionalTime(task.last_seen_at),
       task.consumed, task.cancel_flag, task.token});
  return updated.ok() ? Result<void>() : Result<void>(updated.error());
}

Result<std::optional<TaskRecord>> LoadTaskRecord(db::Connection& config_db,
                                                 const std::string& token) {
  db::Sql condition("token = ");
  Result<std::vector<TaskRecord>> tasks =
      LoadTaskRecords(config_db, {condition.Bind(token)});
  if (!tasks.ok()) return Result<std::optional<TaskRecord>>(tasks.error());
  if (tasks.value().empty()) {
    return Result<std::optional<TaskRecord>>(std::nullopt);
  }
  return Result<std::optional<TaskRecord>>(
      std::move(tasks.value().front()));
}

Result<std::vector<TaskRecord>> LoadTaskRecords(
    db::Connection& config_db, const std::vector<db::Sql>& conditions) {
  const db::Sql query = db::Build({.columns = db::Sql(kColumns),
                                   .from = db::Sql("background_tasks"),
                                   .where = conditions});
  const Result<db::QueryResult> rows =
      config_db.Query(query.text(), query.params());
  if (!rows.ok()) return Result<std::vector<TaskRecord>>(rows.error());
  std::vector<TaskRecord> tasks;
  for (const db::Row& row : rows.value().rows) {
    tasks.push_back(RecordOfRow(row));
  }
  return Result<std::vector<TaskRecord>>(std::move(tasks));
}

}  // namespace cc::server::tasks
