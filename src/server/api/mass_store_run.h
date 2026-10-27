// Port of codechecker_server/api/mass_store_run.py: storing the results
// that `store` uploads. The API process checks the request and prepares a
// background task (store.md §3.2, §3.3); a task worker then stores the
// results into the product's database (store.md §4).

#ifndef CC_SERVER_API_MASS_STORE_RUN_H_
#define CC_SERVER_API_MASS_STORE_RUN_H_

#include <optional>
#include <string>
#include <vector>

#include "server/api/request_failed.h"
#include "server/server_context.h"
#include "server/tasks/task_manager.h"

namespace cc::server::api {

// What to store, and how (Python: SubmittedRunOptions, and the
// store_configuration.json of the task).
struct StoreOptions {
  std::string run_name;
  std::optional<std::string> tag;
  std::string client_version;
  bool force = false;
  std::optional<std::vector<std::string>> trim_path_prefixes;
  std::optional<std::string> description;
  std::string user_name;  // "Anonymous" without authentication.
};

// Checks the store of the Base64 encoded, zlib-compressed ZIP `zip_base64`
// with `options` into the product of `request`, prepares its task and puts
// it on the queue; returns the token of the task (Python:
// __massStoreRun_common() and MassStoreRunInputHandler). Unless
// `asynchronous` (the legacy massStoreRun()), it returns only when the task
// has ended. The caller checks the permission.
ApiResult<std::string> SubmitStore(const Request& request,
                                   const StoreOptions& options,
                                   const std::string& zip_base64,
                                   bool asynchronous);

// Runs the store task `task` in a task worker (Python:
// MassStoreRunTask._implementation()).
tasks::TaskResult<void> RunMassStoreTask(const tasks::QueuedTask& task,
                                         const tasks::StoreTaskInput& input,
                                         ServerContext* context);

}  // namespace cc::server::api

#endif  // CC_SERVER_API_MASS_STORE_RUN_H_
