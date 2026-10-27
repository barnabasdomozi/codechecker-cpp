// Port of codechecker_server/task_executors/task_manager.py: the records
// of the background tasks, their input data folders and the queue that
// takes them to the task workers (cmd-serverside-tasks.md §1-2,
// architecture.md §11.1, §11.7).

#ifndef CC_SERVER_TASKS_TASK_MANAGER_H_
#define CC_SERVER_TASKS_TASK_MANAGER_H_

#include <stdint.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "platform/ipc.h"
#include "platform/signal_receiver.h"
#include "server/database/connection.h"
#include "server/database/database.h"
#include "server/tasks/task_record.h"
#include "util/result.h"

namespace cc::server::tasks {

// Why the body of a task stopped before it finished: it honoured a request
// to cancel it, or it failed (Python: TaskCancelHonoured, or any other
// exception).
struct TaskError {
  bool cancelled = false;
  std::string message;  // Why it failed; the text of Python's exception.
};

// The result of the body of a task, and of the code it calls.
template <typename T>
using TaskResult = Result<T, TaskError>;

// Returns the TaskError that fails a task because of `error`.
inline TaskError TaskFailure(const Error& error) {
  return TaskError{.message = error.message};
}

// The input of a store task (Python: the fields of MassStoreRunTask).
struct StoreTaskInput {
  int64_t product_id = 0;
  int64_t zip_size = 0;  // The size of the decompressed ZIP.
  // The time the API process spent on preparing the task.
  double preparation_seconds = 0.0;
};

// The input of a task for tests (Python: TestingDummyTask).
struct DummyTaskInput {
  int timeout = 0;  // Seconds.
  bool should_fail = false;
};

// A task as the queue takes it to a worker (Python: the AbstractTask that
// is pickled onto the Queue).
struct QueuedTask {
  std::string token;
  std::string data_path;  // The folder of its input data; "" if none.
  std::variant<StoreTaskInput, DummyTaskInput> input;
};

// A task that a worker took from the queue, and the copy of the "done"
// descriptor that came with it (-1 if none), which is closed when the task
// ends.
struct ReceivedTask {
  QueuedTask task;
  platform::FileDescriptor done;
};

// Returns `task` as a message of the queue.
std::string SerializeTask(const QueuedTask& task);

// Returns the task of the queue message `text`; nullopt if it is not one.
std::optional<QueuedTask> ParseTask(std::string_view text);

// Returns the folder of the temporary data of the tasks of `machine_id`:
// <workspace>/codechecker_tasks/<machine_id with unsafe characters
// replaced by '_'>.
std::string TaskDataDirectory(const std::string& workspace,
                              std::string_view machine_id);

// Deletes the temporary data of the tasks of `machine_id` and creates the
// empty folder again (Python: destroy_all_temporary_data()). Failures are
// logged.
void DestroyAllTemporaryData(const std::string& workspace,
                             std::string_view machine_id);

// Marks the unfinished tasks of `machine_id` as dropped, with the comment
// "DROPPED!\n<action>", and returns how many there were (Python:
// drop_all_incomplete_tasks()).
Result<int64_t> DropAllIncompleteTasks(db::Connection& config_db,
                                       std::string_view machine_id,
                                       std::string_view action);

// Creates task records, puts tasks on the queue, and changes the records of
// running tasks. Each process of the server has its own (Python: one
// TaskManager per process). Not thread-safe.
class TaskManager {
 public:
  // Keeps `config_db` and `queue`, which must outlive the manager. The
  // input data of the tasks goes under TaskDataDirectory(`workspace`,
  // `machine_id`).
  TaskManager(db::LazyConnection* config_db, platform::MessageQueue* queue,
              std::string machine_id, std::string workspace)
      : config_db_(config_db),
        queue_(queue),
        machine_id_(std::move(machine_id)),
        workspace_(std::move(workspace)) {}

  TaskManager(const TaskManager&) = delete;
  TaskManager& operator=(const TaskManager&) = delete;

  const std::string& machine_id() const { return machine_id_; }

  // Creates the record of a new task of `user` (nullopt: none) on the
  // product `product_id` (nullopt: the server) and returns its token
  // (Python: allocate_task_record()).
  Result<std::string> AllocateTaskRecord(std::string_view kind,
                                         std::string_view summary,
                                         const std::optional<std::string>& user,
                                         std::optional<int64_t> product_id);

  // Creates a new folder for the input data of the task `token` and
  // returns its path (Python: create_task_data()).
  Result<std::string> CreateTaskData(const std::string& token);

  // Returns the record of the task `token`; fails if there is none (Python:
  // get_task_record()).
  Result<TaskRecord> GetTaskRecord(const std::string& token);

  // Changes the record of the task `token` with `mutator` and writes it
  // back, unless `mutator` fails (Python: _mutate_task_record()). Fails if
  // there is no such task.
  Result<void> MutateTaskRecord(
      const std::string& token,
      const std::function<Result<void>(TaskRecord*)>& mutator);

  // Marks `task` enqueued and puts it on the queue (Python: push_task()).
  // With `done`, the worker gets a copy of it and closes that when the task
  // ends.
  Result<void> PushTask(const QueuedTask& task,
                        const platform::FileDescriptor* done);

  // Returns the next task of the queue, waiting at most `timeout` for it;
  // nullopt if none arrived. Messages that are not tasks are logged and
  // dropped.
  std::optional<ReceivedTask> PopTask(std::chrono::milliseconds timeout);

  // Makes IsShuttingDown() watch `signals` for SIGHUP, with which the
  // supervisor stops the task workers; `signals` must outlive the manager.
  // Only task workers call it.
  void WatchShutdown(platform::SignalReceiver* signals) { signals_ = signals; }

  // Returns whether the server is shutting down: whether SIGHUP arrived
  // (Python: is_shutting_down).
  bool IsShuttingDown();

  // Returns whether the task `token` should stop: the server is shutting
  // down, or the task is enqueued or running and its cancel flag is set
  // (Python: should_cancel()).
  Result<bool> ShouldCancel(const std::string& token);

  // Adds `comment` of `actor` to the task `token` (Python: add_comment()).
  Result<void> AddComment(const std::string& token, std::string_view comment,
                          const std::optional<std::string>& actor);

  // Records that the task `token` is alive (Python: heartbeat()).
  Result<void> Heartbeat(const std::string& token);

  // Records that the task `token` is alive and fails with a cancellation if
  // it should stop (Python: the __cancel_if_needed() of the tasks).
  TaskResult<void> CancelIfRequested(const std::string& token);

 private:
  db::LazyConnection* config_db_;
  platform::MessageQueue* queue_;
  std::string machine_id_;
  std::string workspace_;
  platform::SignalReceiver* signals_ = nullptr;  // Null: no SIGHUP watch.
  bool shutting_down_ = false;
};

}  // namespace cc::server::tasks

#endif  // CC_SERVER_TASKS_TASK_MANAGER_H_
