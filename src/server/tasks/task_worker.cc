#include "server/tasks/task_worker.h"

#include <signal.h>

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/signal_receiver.h"
#include "server/api/mass_store_run.h"
#include "server/server_context.h"
#include "server/tasks/task_manager.h"
#include "server/tasks/task_record.h"
#include "util/logging.h"
#include "util/result.h"

namespace cc::server::tasks {
namespace {

constexpr util::Logger kLog("server");

// How long the queue must stay empty at shutdown before a worker exits
// (Python: WAIT_TIME_FOR_TASK_QUEUE_CLEARING_AT_SERVER_SHUTDOWN), in
// samples of one second.
constexpr int kShutdownEmptySamples = 5;

// How many times a worker tries to record that a task failed (Python:
// max_retries of execute()).
constexpr int kMaxFailureRetries = 10;

// The actor of the comments of the worker.
constexpr std::string_view kExecuteActor = "SYSTEM[AbstractTask::execute()]";

// Runs the test task `token` (Python: TestingDummyTask._implementation()):
// it ticks once a second for `input.timeout` seconds and honours
// cancellation.
TaskResult<void> RunDummyTask(const std::string& token,
                              const DummyTaskInput& input,
                              TaskManager& tasks) {
  for (int counter = 1; counter <= input.timeout; ++counter) {
    if (Result<void> alive = tasks.Heartbeat(token); !alive.ok()) {
      return TaskResult<void>(TaskFailure(alive.error()));
    }
    kLog.Debug(std::format("Dummy task ticking... [{} / {}]", counter,
                           input.timeout));
    const Result<bool> cancel = tasks.ShouldCancel(token);
    if (!cancel.ok()) return TaskResult<void>(TaskFailure(cancel.error()));
    if (cancel.value()) {
      kLog.Info(std::format("Dummy task '{}' was {} at tick [{} / {}]!",
                            token,
                            tasks.IsShuttingDown() ? "KILLED BY SHUTDOWN"
                                                   : "CANCELLED BY ADMIN",
                            counter, input.timeout));
      return TaskResult<void>(TaskError{
          .cancelled = true,
          .message = std::format("Task '{}' honoured CANCEL request.",
                                 token)});
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  if (input.should_fail) {
    return TaskResult<void>(
        TaskError{.message = "Task self-failure as per the user's request."});
  }
  return TaskResult<void>();
}

// Runs the body of `task` (Python: _implementation()).
TaskResult<void> RunBody(const QueuedTask& task, ServerContext* context) {
  if (const auto* store = std::get_if<StoreTaskInput>(&task.input)) {
    return api::RunMassStoreTask(task, *store, context);
  }
  return RunDummyTask(task.token, std::get<DummyTaskInput>(task.input),
                      context->tasks());
}

// Deletes the input data of `task` (Python: destroy_data()).
void DestroyData(const QueuedTask& task) {
  if (task.data_path.empty()) return;
  if (const std::error_code error = platform::RemoveTree(task.data_path)) {
    kLog.Warning(std::format(
        "Failed to remove background task's data_dir at '{}':\n{}",
        task.data_path, error.message()));
    return;
  }
  kLog.Debug(std::format("Wiping temporary data of task '{}' at '{}' ...",
                         task.token, task.data_path));
}

// Records that the task `token` stopped with a comment of `actor`, as
// cancelled or, with `force_dropped`, dropped. Failures are logged.
void Abandon(const std::string& token, std::string_view comment,
             std::string_view actor, bool force_dropped, TaskManager& tasks) {
  const Result<void> abandoned = tasks.MutateTaskRecord(
      token, [comment, actor, force_dropped](TaskRecord* task) {
        AddComment(comment, std::string(actor), task);
        SetAbandoned(force_dropped, task);
        return Result<void>();
      });
  if (!abandoned.ok()) kLog.Error(abandoned.error().message);
}

// Records that the task `token` failed because of `message`, retrying with
// longer and longer waits while the database fails.
void RecordFailure(const std::string& token, const std::string& message,
                   TaskManager& tasks) {
  for (int retries = 0; retries < kMaxFailureRetries;) {
    if (retries > 0) {
      std::this_thread::sleep_for(std::chrono::seconds(1 << retries));
    }
    const Result<void> failed =
        tasks.MutateTaskRecord(token, [&message](TaskRecord* task) {
          AddComment(std::format("FAILED!\nException during execution:\n{}",
                                 message),
                     std::string(kExecuteActor), task);
          return SetFinished(/*successfully=*/false, task);
        });
    if (failed.ok()) return;
    ++retries;
    kLog.Error(std::format(
        "Failed to set task '{}' state to 'FAILED'! (machine: '{}', "
        "executor: #{}, retry: {}/{})",
        token, tasks.machine_id(), platform::ProcessId(), retries,
        kMaxFailureRetries));
    kLog.Error(failed.error().message);
  }
}

// Marks the task `token` running; returns false if the task must not run.
bool StartTask(const std::string& token, TaskManager& tasks) {
  const Result<void> running = tasks.MutateTaskRecord(token, &SetRunning);
  if (running.ok()) return true;
  // A missing record is logged, and the task runs anyway, as in Python.
  if (!tasks.GetTaskRecord(token).ok()) {
    kLog.Error(std::format(
        "Failed to execute task '{}' due to database exception", token));
    return true;
  }
  kLog.Error(std::format(
      "Failed to execute task '{}' due to database exception\n{}", token,
      running.error().message));
  return tasks
      .MutateTaskRecord(token,
                        [](TaskRecord* task) {
                          SetAbandoned(/*force_dropped=*/true, task);
                          return Result<void>();
                        })
      .ok();
}

// Runs `task` and records how it ended (Python: AbstractTask.execute()).
// The "done" descriptor of `task` is closed when this returns.
void Execute(ReceivedTask received, ServerContext* context) {
  TaskManager& tasks = context->tasks();
  const QueuedTask& task = received.task;
  const Result<bool> cancel = tasks.ShouldCancel(task.token);
  if (!cancel.ok()) {
    kLog.Error(cancel.error().message);
    return;
  }
  if (cancel.value()) {
    Abandon(task.token, "CANCEL!\nTask cancelled before execution began!",
            kExecuteActor, /*force_dropped=*/false, tasks);
    return;
  }
  if (!StartTask(task.token, tasks)) return;
  kLog.Debug(std::format("Task '{}' running on machine '{}' executor #{}",
                         task.token, tasks.machine_id(),
                         platform::ProcessId()));
  const TaskResult<void> result = RunBody(task, context);
  if (result.ok()) {
    kLog.Debug(std::format("Task '{}' finished on machine '{}' executor #{}",
                           task.token, tasks.machine_id(),
                           platform::ProcessId()));
    const Result<void> finished =
        tasks.MutateTaskRecord(task.token, [](TaskRecord* record) {
          return SetFinished(/*successfully=*/true, record);
        });
    if (!finished.ok()) {
      kLog.Error(std::format(
          "Failed to set task '{}' finished due to database exception:\n{}",
          task.token, finished.error().message));
    }
  } else if (result.error().cancelled) {
    if (!tasks.IsShuttingDown()) {
      Abandon(task.token,
              "CANCEL!\nCancel request of admin honoured by task.",
              kExecuteActor, /*force_dropped=*/false, tasks);
    } else {
      Abandon(task.token,
              "SHUTDOWN!\nTask honoured graceful cancel signal generated by "
              "server shutdown.",
              kExecuteActor, /*force_dropped=*/true, tasks);
    }
    kLog.Debug(std::format(
        "Task '{}' honoured the administrator's cancel request at:\n{}",
        task.token, result.error().message));
  } else {
    kLog.Error(std::format(
        "Failed to execute task '{}' on machine '{}' executor #{}: {}",
        task.token, tasks.machine_id(), platform::ProcessId(),
        result.error().message));
    RecordFailure(task.token, result.error().message, tasks);
  }
  DestroyData(task);
}

// Records that `received` never ran because the server shut down, and
// deletes its data (Python: _drop_task_at_shutdown()).
void DropAtShutdown(const ReceivedTask& received, TaskManager& tasks) {
  kLog.Debug(std::format("Dropping task '{}' due to server shutdown...",
                         received.task.token));
  (void)tasks.MutateTaskRecord(received.task.token, [](TaskRecord* task) {
    AddComment("SHUTDOWN!\nTask never started due to the server shutdown!",
               "SYSTEM", task);
    SetAbandoned(/*force_dropped=*/true, task);
    return Result<void>();
  });
  DestroyData(received.task);
}

}  // namespace

int RunTaskWorker(ServerContext* context) {
  std::error_code error;
  std::optional<platform::SignalReceiver> signals =
      platform::SignalReceiver::Create({SIGHUP, SIGINT, SIGTERM}, &error);
  if (!signals) {
    kLog.Error(std::format("Couldn't receive signals: {}", error.message()));
    return 1;
  }
  TaskManager& tasks = context->tasks();
  tasks.WatchShutdown(&*signals);
  // Waiting a second at a time lets the worker see SIGHUP.
  constexpr std::chrono::seconds kPollTime(1);
  while (!tasks.IsShuttingDown()) {
    std::optional<ReceivedTask> received = tasks.PopTask(kPollTime);
    if (!received) continue;
    kLog.Debug(std::format("Executor PID {} popped task '{}' ...",
                           platform::ProcessId(), received->task.token));
    Execute(std::move(*received), context);
  }
  // The supervisor sends SIGHUP only when the server shuts down. The tasks
  // still queued would be lost, so they are dropped; the queue must stay
  // empty for a while, because another worker may take a task first.
  for (int empty_samples = 0; empty_samples < kShutdownEmptySamples;) {
    const std::optional<ReceivedTask> received = tasks.PopTask(kPollTime);
    if (!received) {
      ++empty_samples;
      continue;
    }
    empty_samples = 0;
    DropAtShutdown(*received, tasks);
  }
  kLog.Debug(std::format("Task executor subprocess PID {} exited main loop.",
                         platform::ProcessId()));
  return 0;
}

}  // namespace cc::server::tasks
