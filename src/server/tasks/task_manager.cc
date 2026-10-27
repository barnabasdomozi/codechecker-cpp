#include "server/tasks/task_manager.h"

#include <signal.h>
#include <stdint.h>
#include <yyjson.h>

#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "platform/filesystem.h"
#include "platform/ipc.h"
#include "platform/process.h"
#include "server/database/connection.h"
#include "server/database/sql.h"
#include "server/tasks/task_record.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/uuid.h"

namespace cc::server::tasks {
namespace {

constexpr util::Logger kLog("server");

// How many tokens AllocateTaskRecord() tries (Python:
// MAX_TOKEN_RANDOM_RETRIES).
constexpr int kMaxTokenRetries = 10;

}  // namespace

std::string SerializeTask(const QueuedTask& task) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_strcpy(doc, root, "token", task.token.c_str());
  yyjson_mut_obj_add_strcpy(doc, root, "data_path", task.data_path.c_str());
  if (const auto* store = std::get_if<StoreTaskInput>(&task.input)) {
    yyjson_mut_obj_add_str(doc, root, "kind", "store");
    yyjson_mut_obj_add_int(doc, root, "product_id", store->product_id);
    yyjson_mut_obj_add_int(doc, root, "zip_size", store->zip_size);
    yyjson_mut_obj_add_real(doc, root, "preparation_seconds",
                            store->preparation_seconds);
  } else {
    const DummyTaskInput& dummy = std::get<DummyTaskInput>(task.input);
    yyjson_mut_obj_add_str(doc, root, "kind", "dummy");
    yyjson_mut_obj_add_int(doc, root, "timeout", dummy.timeout);
    yyjson_mut_obj_add_bool(doc, root, "should_fail", dummy.should_fail);
  }
  return util::DumpJson(root);
}

std::optional<QueuedTask> ParseTask(std::string_view text) {
  const Result<util::JsonDocument> document = util::ParseJson(text);
  if (!document.ok()) return std::nullopt;
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  auto number = [root](std::string_view key) {
    return util::JsonToInt(util::JsonGet(root, key)).value_or(0);
  };
  QueuedTask task = {
      .token = util::JsonString(util::JsonGet(root, "token")),
      .data_path = util::JsonString(util::JsonGet(root, "data_path")),
      .input = DummyTaskInput()};
  const std::string kind = util::JsonString(util::JsonGet(root, "kind"));
  if (kind == "store") {
    task.input = StoreTaskInput{
        .product_id = number("product_id"),
        .zip_size = number("zip_size"),
        .preparation_seconds = yyjson_get_num(
            util::JsonGet(root, "preparation_seconds"))};
  } else if (kind == "dummy") {
    task.input = DummyTaskInput{
        .timeout = static_cast<int>(number("timeout")),
        .should_fail = yyjson_get_bool(util::JsonGet(root, "should_fail"))};
  } else {
    return std::nullopt;
  }
  return task;
}

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
  db::Sql machine("machine_id = ");
  Result<std::vector<TaskRecord>> tasks = LoadTaskRecords(
      config_db,
      {machine.Bind(std::string(machine_id)),
       db::In("status", std::vector<std::string>{"allocated", "enqueued",
                                                 "running"})});
  if (!tasks.ok()) return Result<int64_t>(tasks.error());
  for (TaskRecord& task : tasks.value()) {
    AddComment(std::format("DROPPED!\n{}", action), "SYSTEM", &task);
    SetAbandoned(/*force_dropped=*/true, &task);
    if (Result<void> updated = UpdateTaskRecord(config_db, task);
        !updated.ok()) {
      return Result<int64_t>(updated.error());
    }
  }
  return Result<int64_t>(static_cast<int64_t>(tasks.value().size()));
}

Result<std::string> TaskManager::AllocateTaskRecord(
    std::string_view kind, std::string_view summary,
    const std::optional<std::string>& user,
    std::optional<int64_t> product_id) {
  Result<db::Connection*> config_db = config_db_->Get();
  if (!config_db.ok()) return Result<std::string>(config_db.error());
  // A failed insert means that the token is used already.
  for (int i = 0; i < kMaxTokenRetries; ++i) {
    std::optional<std::string> token = util::RandomHexToken(kTokenLength);
    if (!token) {
      return Result<std::string>(
          Error{.message = "Failed to generate secure random bytes."});
    }
    const TaskRecord task =
        NewTaskRecord(*token, std::string(kind), std::string(summary),
                      machine_id_, user, product_id);
    if (InsertTaskRecord(*config_db.value(), task).ok()) {
      return Result<std::string>(std::move(*token));
    }
  }
  return Result<std::string>(util::KeyError(std::format(
      "Failed to generate a unique ID for task {} ({}) after {} retries!",
      kind, summary, kMaxTokenRetries)));
}

Result<std::string> TaskManager::CreateTaskData(const std::string& token) {
  const std::string root = TaskDataDirectory(workspace_, machine_id_);
  if (const std::error_code error = platform::EnsureDirs(root)) {
    return Result<std::string>(Error{.message = error.message()});
  }
  std::optional<std::string> directory =
      platform::MakeTempDir(root, token + "-");
  if (!directory) {
    return Result<std::string>(Error{
        .message = std::format("Couldn't create a folder in '{}'.", root)});
  }
  return Result<std::string>(std::move(*directory));
}

Result<TaskRecord> TaskManager::GetTaskRecord(const std::string& token) {
  Result<db::Connection*> config_db = config_db_->Get();
  if (!config_db.ok()) return Result<TaskRecord>(config_db.error());
  Result<std::optional<TaskRecord>> task =
      LoadTaskRecord(*config_db.value(), token);
  if (!task.ok()) return Result<TaskRecord>(task.error());
  if (!task.value()) {
    return Result<TaskRecord>(util::KeyError(std::format(
        "No task record for token '{}' in the database", token)));
  }
  return Result<TaskRecord>(std::move(*task.value()));
}

Result<void> TaskManager::MutateTaskRecord(
    const std::string& token,
    const std::function<Result<void>(TaskRecord*)>& mutator) {
  Result<TaskRecord> task = GetTaskRecord(token);
  if (!task.ok()) return Result<void>(task.error());
  if (Result<void> changed = mutator(&task.value()); !changed.ok()) {
    return changed;
  }
  Result<db::Connection*> config_db = config_db_->Get();
  if (!config_db.ok()) return Result<void>(config_db.error());
  return UpdateTaskRecord(*config_db.value(), task.value());
}

Result<void> TaskManager::PushTask(const QueuedTask& task,
                                   const platform::FileDescriptor* done) {
  // The record is enqueued before the task is, so the users see the time;
  // a process that is killed in between leaves a task no worker runs, as
  // in the reference.
  if (Result<void> enqueued = MutateTaskRecord(task.token, &SetEnqueued);
      !enqueued.ok()) {
    return enqueued;
  }
  if (const std::error_code error =
          queue_->Send(SerializeTask(task), done ? done->get() : -1)) {
    return Result<void>(Error{.message = error.message()});
  }
  return Result<void>();
}

std::optional<ReceivedTask> TaskManager::PopTask(
    std::chrono::milliseconds timeout) {
  std::optional<platform::MessageQueue::Message> message =
      queue_->Receive(timeout);
  if (!message) return std::nullopt;
  std::optional<QueuedTask> task = ParseTask(message->text);
  if (!task) {
    kLog.Error(std::format("Invalid message in the task queue: {}",
                           util::Repr(message->text)));
    return std::nullopt;
  }
  return ReceivedTask{.task = std::move(*task),
                      .done = std::move(message->fd)};
}

bool TaskManager::IsShuttingDown() {
  while (signals_ != nullptr && !shutting_down_) {
    const std::optional<int> signum =
        signals_->Wait(std::chrono::milliseconds(0));
    if (!signum) break;
    if (*signum == SIGHUP) {
      kLog.Debug(std::format(
          "{}: Received SIGHUP ({}), preparing for shutdown ...",
          platform::ProcessId(), SIGHUP));
      shutting_down_ = true;
    }
  }
  return shutting_down_;
}

Result<bool> TaskManager::ShouldCancel(const std::string& token) {
  const Result<TaskRecord> task = GetTaskRecord(token);
  if (!task.ok()) return Result<bool>(task.error());
  const bool flagged =
      (task.value().status == "enqueued" || task.value().status == "running") &&
      task.value().cancel_flag;
  return Result<bool>(IsShuttingDown() || flagged);
}

Result<void> TaskManager::AddComment(const std::string& token,
                                     std::string_view comment,
                                     const std::optional<std::string>& actor) {
  return MutateTaskRecord(token, [comment, &actor](TaskRecord* task) {
    tasks::AddComment(comment, actor, task);
    return Result<void>();
  });
}

Result<void> TaskManager::Heartbeat(const std::string& token) {
  return MutateTaskRecord(token, [](TaskRecord* task) {
    tasks::Heartbeat(task);
    return Result<void>();
  });
}

TaskResult<void> TaskManager::CancelIfRequested(const std::string& token) {
  if (Result<void> alive = Heartbeat(token); !alive.ok()) {
    return TaskResult<void>(TaskFailure(alive.error()));
  }
  const Result<bool> cancel = ShouldCancel(token);
  if (!cancel.ok()) return TaskResult<void>(TaskFailure(cancel.error()));
  if (!cancel.value()) return TaskResult<void>();
  return TaskResult<void>(TaskError{
      .cancelled = true,
      .message = std::format("Task '{}' honoured CANCEL request.", token)});
}

}  // namespace cc::server::tasks
