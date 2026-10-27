// Port of BackgroundTask of codechecker_server/database/config_db_model.py:
// the record of a background task in the configuration database, and the
// changes of its status (cmd-serverside-tasks.md §1).

#ifndef CC_SERVER_TASKS_TASK_RECORD_H_
#define CC_SERVER_TASKS_TASK_RECORD_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "server/database/connection.h"
#include "server/database/sql.h"
#include "util/result.h"

namespace cc::server::tasks {

// The number of characters of a task token (Python:
// BackgroundTask._token_length).
inline constexpr int kTokenLength = 64;

// A row of the background_tasks table. The times are UTC times in the form
// of util/datetime.h.
struct TaskRecord {
  std::string token = {};
  std::optional<std::string> machine_id = {};
  std::string kind = {};
  // allocated, enqueued, running, completed, failed, cancelled or dropped.
  std::string status = {};
  std::optional<int64_t> product_id = {};
  std::optional<std::string> username = {};
  std::string summary = {};
  std::optional<std::string> comments = {};
  std::optional<int64_t> enqueued_at = {};
  std::optional<int64_t> started_at = {};
  std::optional<int64_t> finished_at = {};
  std::optional<int64_t> last_seen_at = {};
  bool consumed = false;
  bool cancel_flag = false;
};

// Returns the record of a new task: allocated, and last seen now (Python:
// BackgroundTask()).
TaskRecord NewTaskRecord(std::string token, std::string kind,
                         std::string summary, std::string machine_id,
                         std::optional<std::string> username,
                         std::optional<int64_t> product_id);

// Appends `comment` of `actor` ("<unknown>" if nullopt) to the comments of
// `task`, with the time (Python: add_comment()).
void AddComment(std::string_view comment,
                const std::optional<std::string>& actor, TaskRecord* task);

// Sets the last seen time of `task` to now if it is enqueued or running
// (Python: heartbeat()).
void Heartbeat(TaskRecord* task);

// Changes the status of `task` from allocated to enqueued, from enqueued to
// running, and from running to completed (`successfully`) or failed, and
// sets the time of the change (Python: set_enqueued(), set_running(),
// set_finished()). Another status fails with Python's message "Invalid
// transition '<old>' -> '<new>'".
Result<void> SetEnqueued(TaskRecord* task);
Result<void> SetRunning(TaskRecord* task);
Result<void> SetFinished(bool successfully, TaskRecord* task);

// Sets `task` cancelled if its cancel flag is set and not
// `force_dropped`, otherwise dropped (Python: set_abandoned()).
void SetAbandoned(bool force_dropped, TaskRecord* task);

// Returns whether `task` ended: it is not allocated, enqueued or running
// (Python: is_in_terminated_state).
bool IsTerminated(const TaskRecord& task);

// Returns whether setting the cancel flag of `task` means something
// (Python: can_be_cancelled).
bool CanBeCancelled(const TaskRecord& task);

// Inserts `task` into the configuration database `config_db`.
Result<void> InsertTaskRecord(db::Connection& config_db,
                              const TaskRecord& task);

// Writes the fields of `task` that change into its row.
Result<void> UpdateTaskRecord(db::Connection& config_db,
                              const TaskRecord& task);

// Returns the task `token`; nullopt if there is none.
Result<std::optional<TaskRecord>> LoadTaskRecord(db::Connection& config_db,
                                                 const std::string& token);

// Returns the tasks that match all of `conditions`, which are about the
// columns of background_tasks, in the order of the database.
Result<std::vector<TaskRecord>> LoadTaskRecords(
    db::Connection& config_db, const std::vector<db::Sql>& conditions);

}  // namespace cc::server::tasks

#endif  // CC_SERVER_TASKS_TASK_RECORD_H_
