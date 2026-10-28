#include "client/task_client.h"

#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "api/thrift/codeCheckerServersideTaskService.h"
#include "api/thrift/tasks_types.h"
#include "client/client.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::client {
namespace {

constexpr util::Logger kLog("system");

// The limits of the distance between two status queries, in microseconds.
constexpr int64_t kProbeDistanceMin = 5 * util::kMicrosecondsPerSecond;
constexpr int64_t kProbeDistanceMax = 120 * util::kMicrosecondsPerSecond;
// A task that made no progress for this long is taken as lost.
constexpr int64_t kProgressTimeout = 3600 * util::kMicrosecondsPerSecond;
constexpr int kMaxConsecutiveRequestFailures = 10;

// What one status query found out.
struct TaskState {
  TaskStatus::type status = TaskStatus::ALLOCATED;
  // When the task last made progress (completed, sent a heartbeat or
  // started), in UTC microseconds; nullopt if it did not start yet.
  std::optional<int64_t> progressed_at = std::nullopt;
};

// Returns the state of the task `token`, asking again after failed queries
// (Python: _query_task_status()).
Result<TaskState> QueryTaskState(std::string_view token, TaskClient& client) {
  int request_failures = 0;
  TaskInfo info;
  while (true) {
    const Result<void> called = client.Call(
        "getTaskInfo", [&](codeCheckerServersideTaskServiceClient& tasks) {
          tasks.getTaskInfo(info, std::string(token));
        });
    if (called.ok()) break;
    ++request_failures;
    if (request_failures > kMaxConsecutiveRequestFailures) {
      return Result<TaskState>(called.error());
    }
    kLog.Info(std::format("Retrying task status query [{} / {} retries] ...",
                          request_failures, kMaxConsecutiveRequestFailures));
  }
  TaskState state{.status = info.status};
  for (const int64_t epoch : {info.completedAtEpoch, info.lastHeartbeatEpoch,
                              info.startedAtEpoch}) {
    if (epoch != 0) {
      state.progressed_at = epoch * util::kMicrosecondsPerSecond;
      break;
    }
  }
  return Result<TaskState>(state);
}

// Returns the name of `status`, for example "COMPLETED"; its number if the
// server sent a status this client does not know.
std::string StatusName(TaskStatus::type status) {
  const auto it = _TaskStatus_VALUES_TO_NAMES.find(status);
  return it == _TaskStatus_VALUES_TO_NAMES.end()
             ? std::to_string(static_cast<int>(status))
             : std::string(it->second);
}

// Returns the distance to the next status query after `probe_distance`,
// for a task that last progressed at `progressed_at`, and before that at
// `last_progressed_at`. Python adjusts the distance like TCP's AIMD: closer
// probes while the task progresses well, further apart while it does not.
// As in Python, the difference is taken the wrong way round, so it is never
// larger than twice the distance.
int64_t NextProbeDistance(int64_t probe_distance,
                          std::optional<int64_t> last_progressed_at,
                          int64_t progressed_at) {
  if (last_progressed_at) {
    const int64_t between = *last_progressed_at - progressed_at;
    if (between != 0 && between <= 2 * probe_distance) {
      return std::max(probe_distance / 2, kProbeDistanceMin);
    }
  }
  return std::min(probe_distance + util::kMicrosecondsPerSecond,
                  kProbeDistanceMax);
}

// Returns the UTC time `time` as Python's str() of an aware datetime in UTC
// writes it.
std::string FormatUtcTime(int64_t time) {
  return util::FormatPythonTime(time) + "+00:00";
}

}  // namespace

Result<std::string> AwaitTaskTermination(std::string_view token,
                                         TaskClient& client) {
  int64_t probe_distance = kProbeDistanceMin;
  std::optional<int64_t> last_progressed_at;
  while (true) {
    const Result<TaskState> state = QueryTaskState(token, client);
    if (!state.ok()) return Result<std::string>(state.error());
    const TaskStatus::type status = state.value().status;
    if (status == TaskStatus::COMPLETED || status == TaskStatus::FAILED ||
        status == TaskStatus::CANCELLED || status == TaskStatus::DROPPED) {
      return Result<std::string>(StatusName(status));
    }
    if (const std::optional<int64_t> progressed_at =
            state.value().progressed_at) {
      const int64_t since_progress = util::UtcNow() - *progressed_at;
      if (since_progress >= kProgressTimeout) {
        kLog.Error(std::format(
            "'{}' timeout elapsed since task last progressed at '{}', "
            "considering hung/locked out/lost/failed...",
            util::FormatTimedelta(kProgressTimeout),
            FormatUtcTime(*progressed_at)));
        return Result<std::string>(Error{
            .message = std::format(
                "Task '{}' is still '{}', but did not have any progress for "
                "'{}' ({} seconds)!",
                token, StatusName(status),
                util::FormatTimedelta(since_progress),
                util::FloatRepr(static_cast<double>(since_progress) /
                                util::kMicrosecondsPerSecond))});
      }
      probe_distance = NextProbeDistance(probe_distance, last_progressed_at,
                                         *progressed_at);
      last_progressed_at = progressed_at;
    }
    kLog.Debug(std::format(
        "Waiting {:f} seconds ({}) before querying the server...",
        static_cast<double>(probe_distance) / util::kMicrosecondsPerSecond,
        util::FormatTimedelta(probe_distance)));
    std::this_thread::sleep_for(std::chrono::microseconds(probe_distance));
  }
}

}  // namespace cc::client
