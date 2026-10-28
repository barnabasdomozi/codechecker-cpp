// Port of await_task_termination() of codechecker_client/task_client.py:
// waiting for a server-side task to end (cmd-serverside-tasks.md §4).

#ifndef CC_CLIENT_TASK_CLIENT_H_
#define CC_CLIENT_TASK_CLIENT_H_

#include <string>
#include <string_view>

#include "client/client.h"
#include "util/result.h"

namespace cc::client {

// Waits until the task `token` of the server of `client` ends and returns
// the name of its final status, for example "COMPLETED". The status is
// queried 5 seconds to 2 minutes apart, the distance adjusted to the
// progress of the task (Python: await_task_termination() with its
// defaults). Fails if the task made no progress for an hour (with the text
// of Python's TaskTimeoutError), or if the status query failed more than 10
// times in a row (already logged).
Result<std::string> AwaitTaskTermination(std::string_view token,
                                         TaskClient& client);

}  // namespace cc::client

#endif  // CC_CLIENT_TASK_CLIENT_H_
