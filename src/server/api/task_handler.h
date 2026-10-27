// Port of codechecker_server/api/tasks.py: the Tasks service, which shows
// the background tasks to their submitters and to administrators, and
// cancels them (cmd-serverside-tasks.md §3).

#ifndef CC_SERVER_API_TASK_HANDLER_H_
#define CC_SERVER_API_TASK_HANDLER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "api/thrift/codeCheckerServersideTaskService.h"
#include "api/thrift/shared_types.h"
#include "api/thrift/tasks_types.h"
#include "server/database/connection.h"
#include "server/server_context.h"
#include "server/tasks/task_record.h"

namespace cc::server::api {

// Answers the Tasks calls of one request. Errors are thrown as
// RequestFailed.
class TaskHandler final : public codeCheckerServersideTaskServiceIf {
 public:
  explicit TaskHandler(const Request& request) : request_(request) {}

  TaskHandler(const TaskHandler&) = delete;
  TaskHandler& operator=(const TaskHandler&) = delete;

  void getTaskInfo(TaskInfo& result, const ::TaskToken& token) override;
  void getTasks(std::vector<AdministratorTaskInfo>& result,
                const TaskFilter& filters) override;
  bool cancelTask(const ::TaskToken& token) override;
  void createDummyTask(::TaskToken& result, int32_t timeout,
                       bool should_fail) override;

 private:
  // Returns the connection to the configuration database; throws DATABASE
  // if it cannot be opened.
  db::Connection& ConfigDb();

  // Returns whether the caller has `permission` on the product
  // `product_id`, or on the server for nullopt (Python:
  // permissions.require_permission()).
  bool Has(Permission::type permission, std::optional<int64_t> product_id);

  // Returns the user name of the caller; nullopt without a session (Python:
  // _get_username()).
  std::optional<std::string> User() const;

  // Returns whether the caller may see or cancel `task` (null if there is
  // no such task): its submitter may, others need `product_permission` on
  // the product of the task, or SUPERUSER for a server task. Sets
  // `*own_task` if the caller submitted it.
  bool MayAccess(const tasks::TaskRecord* task,
                 Permission::type product_permission, bool* own_task);

  Request request_;
};

}  // namespace cc::server::api

#endif  // CC_SERVER_API_TASK_HANDLER_H_
