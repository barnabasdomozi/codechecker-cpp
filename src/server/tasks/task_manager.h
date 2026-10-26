// Port of the parts of codechecker_server/task_executors/task_manager.py
// that the server runs when it starts and stops: dropping the background
// tasks a server will never finish, and their temporary data
// (cmd-serverside-tasks.md, server.md §5).

#ifndef CC_SERVER_TASKS_TASK_MANAGER_H_
#define CC_SERVER_TASKS_TASK_MANAGER_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "server/database/connection.h"
#include "util/result.h"

namespace cc::server::tasks {

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

}  // namespace cc::server::tasks

#endif  // CC_SERVER_TASKS_TASK_MANAGER_H_
