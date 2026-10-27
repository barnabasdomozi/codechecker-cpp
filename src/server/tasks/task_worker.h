// Port of codechecker_server/task_executors/main.py and of
// AbstractTask.execute() in abstract_task.py: the task worker processes,
// which take background tasks from the queue and run them
// (cmd-serverside-tasks.md §2, architecture.md §11.7).

#ifndef CC_SERVER_TASKS_TASK_WORKER_H_
#define CC_SERVER_TASKS_TASK_WORKER_H_

#include "server/server_context.h"

namespace cc::server::tasks {

// Runs a task worker process (Python: executor()): runs the tasks of the
// queue of `context` until the supervisor sends SIGHUP, then drops the
// tasks still queued. Termination signals are ignored, so a worker can
// clean up after itself. Returns the exit code of the process.
int RunTaskWorker(ServerContext* context);

}  // namespace cc::server::tasks

#endif  // CC_SERVER_TASKS_TASK_WORKER_H_
