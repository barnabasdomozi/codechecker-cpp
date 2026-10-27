#include "server/api/task_handler.h"

#include <stdint.h>

#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/shared_types.h"
#include "api/thrift/tasks_types.h"
#include "platform/subprocess.h"
#include "server/api/report_filter.h"
#include "server/api/request_failed.h"
#include "server/auth/permissions.h"
#include "server/database/connection.h"
#include "server/database/sql.h"
#include "server/server_context.h"
#include "server/tasks/task_manager.h"
#include "server/tasks/task_record.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/strings.h"

namespace cc::server::api {
namespace {

constexpr util::Logger kLog("server");

// Returns the Unix time of the UTC time `time`; nullopt for nullopt
// (Python: _db_timestamp_to_posix_epoch()).
std::optional<int64_t> Epoch(std::optional<int64_t> time) {
  if (!time) return std::nullopt;
  return *time / util::kMicrosecondsPerSecond;
}

// Returns the UTC time of the Unix time `epoch` as a parameter (Python:
// _posix_epoch_to_db_timestamp()).
db::Param EpochParam(int64_t epoch) {
  return util::FormatDatabaseTime(epoch * util::kMicrosecondsPerSecond);
}

// Returns the API status of the status `name` of a record.
TaskStatus::type StatusOf(const std::string& name) {
  for (const auto& [value, status] : _TaskStatus_VALUES_TO_NAMES) {
    if (util::AsciiLower(status) == name) {
      return static_cast<TaskStatus::type>(value);
    }
  }
  throw MakeRequestFailed(ErrorCode::GENERAL, util::Repr(name));
}

// Returns `task` as the API sends it (Python: _make_task_info()).
TaskInfo ToTaskInfo(const tasks::TaskRecord& task) {
  TaskInfo info;
  info.__set_token(task.token);
  info.__set_taskKind(task.kind);
  info.__set_status(StatusOf(task.status));
  info.__set_productId(task.product_id.value_or(0));
  if (task.username) info.__set_actorUsername(*task.username);
  info.__set_summary(task.summary);
  if (task.comments) info.__set_comments(*task.comments);
  if (const std::optional<int64_t> time = Epoch(task.enqueued_at)) {
    info.__set_enqueuedAtEpoch(*time);
  }
  if (const std::optional<int64_t> time = Epoch(task.started_at)) {
    info.__set_startedAtEpoch(*time);
  }
  if (const std::optional<int64_t> time = Epoch(task.finished_at)) {
    info.__set_completedAtEpoch(*time);
  }
  if (const std::optional<int64_t> time = Epoch(task.last_seen_at)) {
    info.__set_lastHeartbeatEpoch(*time);
  }
  info.__set_cancelFlagSet(task.cancel_flag);
  return info;
}

// Returns the condition that `column` is LIKE any of `patterns`, with '*'
// as the wildcard and case-insensitive (Python: or_(column.ilike(conv(p)))).
db::Sql AnyLike(std::string_view column,
                const std::vector<std::string>& patterns) {
  std::vector<db::Sql> any;
  for (const std::string& pattern : patterns) {
    any.push_back(db::ILike(db::Sql(column), Conv(pattern)));
  }
  return db::Or(any);
}

// Adds to `conditions` that `column` compares with `op` to the Unix time
// `epoch`, unless it is 0.
void AddTimeCondition(std::string_view column, std::string_view op,
                      int64_t epoch, std::vector<db::Sql>* conditions) {
  if (epoch == 0) return;
  db::Sql condition(std::format("{} {} ", column, op));
  conditions->push_back(condition.Bind(EpochParam(epoch)));
}

// Adds to `conditions` the condition of the Ternary `flag` on the boolean
// `column`.
void AddFlagCondition(std::string_view column, Ternary::type flag,
                      std::vector<db::Sql>* conditions) {
  if (flag == Ternary::OFF) {
    conditions->emplace_back(std::format("{} IS false", column));
  } else if (flag == Ternary::ON) {
    conditions->emplace_back(std::format("{} IS true", column));
  }
}

// Returns the conditions of `filters` on background_tasks (Python: the AND
// list of getTasks()).
std::vector<db::Sql> FilterConditions(const TaskFilter& filters) {
  std::vector<db::Sql> conditions;
  if (!filters.tokens.empty()) {
    conditions.push_back(AnyLike("token", filters.tokens));
  }
  if (!filters.machineIDs.empty()) {
    conditions.push_back(AnyLike("machine_id", filters.machineIDs));
  }
  if (!filters.kinds.empty()) {
    conditions.push_back(AnyLike("kind", filters.kinds));
  }
  if (!filters.statuses.empty()) {
    std::vector<std::string> names;
    for (const TaskStatus::type status : filters.statuses) {
      names.push_back(
          util::AsciiLower(_TaskStatus_VALUES_TO_NAMES.at(status)));
    }
    conditions.push_back(db::In("status", names));
  }
  if (filters.__isset.usernames) {
    conditions.push_back(filters.usernames.empty()
                             ? db::Sql("username IS NULL")
                             : AnyLike("username", filters.usernames));
  }
  if (filters.__isset.productIDs) {
    conditions.push_back(filters.productIDs.empty()
                             ? db::Sql("product_id IS NULL")
                             : db::In("product_id", filters.productIDs));
  }
  AddTimeCondition("enqueued_at", "<=", filters.enqueuedBeforeEpoch,
                   &conditions);
  AddTimeCondition("enqueued_at", ">=", filters.enqueuedAfterEpoch,
                   &conditions);
  AddTimeCondition("started_at", "<=", filters.startedBeforeEpoch,
                   &conditions);
  AddTimeCondition("started_at", ">=", filters.startedAfterEpoch,
                   &conditions);
  AddTimeCondition("finished_at", "<=", filters.completedBeforeEpoch,
                   &conditions);
  AddTimeCondition("finished_at", ">=", filters.completedAfterEpoch,
                   &conditions);
  AddTimeCondition("last_seen_at", "<=", filters.heartbeatBeforeEpoch,
                   &conditions);
  AddTimeCondition("last_seen_at", ">=", filters.heartbeatAfterEpoch,
                   &conditions);
  AddFlagCondition("cancel_flag", filters.cancelFlag, &conditions);
  AddFlagCondition("consumed", filters.consumedFlag, &conditions);
  return conditions;
}

}  // namespace

db::Connection& TaskHandler::ConfigDb() {
  return *ValueOrThrow(request_.server->config_db().Get());
}

bool TaskHandler::Has(Permission::type permission,
                      std::optional<int64_t> product_id) {
  return ValueOrThrow(auth::RequirePermission(
      permission, {.config_db = &ConfigDb(), .product_id = product_id},
      {.session = request_.session,
       .authentication_enabled = request_.server->sessions().enabled()}));
}

std::optional<std::string> TaskHandler::User() const {
  if (request_.session == nullptr) return std::nullopt;
  return request_.session->user;
}

bool TaskHandler::MayAccess(const tasks::TaskRecord* task,
                            Permission::type product_permission,
                            bool* own_task) {
  *own_task = task != nullptr && task->username == User();
  if (*own_task) return true;
  if (task == nullptr || !task->product_id) {
    return Has(Permission::SUPERUSER, std::nullopt);
  }
  const bool product_exists =
      ValueOrThrow(db::QueryValue(ConfigDb(),
                                  "SELECT id FROM products WHERE id = ?",
                                  {*task->product_id}))
          .has_value();
  if (!product_exists) {
    kLog.Error(std::format(
        "No product with ID '{}', but a task is associated with it.",
        *task->product_id));
    return false;
  }
  return Has(product_permission, task->product_id);
}

void TaskHandler::getTaskInfo(TaskInfo& result, const ::TaskToken& token) {
  std::optional<tasks::TaskRecord> task =
      ValueOrThrow(tasks::LoadTaskRecord(ConfigDb(), token));
  bool own_task = false;
  if (!MayAccess(task ? &*task : nullptr, Permission::PRODUCT_ACCESS,
                 &own_task)) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        "The task does not exist, or you are not authorized to view the "
        "task. (Only the task's submitter, people with access to the task's "
        "product, or a SUPERUSER can view the task.)");
  }
  if (!task) {
    throw MakeRequestFailed(ErrorCode::GENERAL, "The task does not exist.");
  }
  result = ToTaskInfo(*task);
  // The submitter has now seen how the task ended.
  if (own_task && tasks::IsTerminated(*task)) {
    task->consumed = true;
    ThrowIfFailed(tasks::UpdateTaskRecord(ConfigDb(), *task));
  }
}

void TaskHandler::getTasks(std::vector<AdministratorTaskInfo>& result,
                           const TaskFilter& filters) {
  if (filters.__isset.productIDs && filters.productIDs.empty() &&
      !Has(Permission::SUPERUSER, std::nullopt)) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        "Querying service tasks (not associated with a product) requires "
        "SUPERUSER privileges!");
  }
  std::vector<std::string> no_access;
  for (const int64_t product_id : filters.productIDs) {
    if (Has(Permission::PRODUCT_ACCESS, product_id)) continue;
    no_access.push_back(
        ValueOrThrow(db::QueryValue(ConfigDb(),
                                    "SELECT endpoint FROM products WHERE "
                                    "id = ?",
                                    {product_id}))
            .value_or("None"));
  }
  if (!no_access.empty()) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        std::format("Querying product tasks requires PRODUCT_ACCESS rights, "
                    "but it is missing from product(s): '{}'!",
                    util::Join(no_access, "', '")));
  }
  // Server tasks are shown to SUPERUSERs, product tasks to those who have
  // access to the product.
  std::optional<bool> superuser;
  std::map<int64_t, bool> product_access;
  for (const tasks::TaskRecord& task : ValueOrThrow(
           tasks::LoadTaskRecords(ConfigDb(), FilterConditions(filters)))) {
    if (!task.product_id) {
      if (!superuser) superuser = Has(Permission::SUPERUSER, std::nullopt);
      if (!*superuser) continue;
    } else {
      auto access = product_access.find(*task.product_id);
      if (access == product_access.end()) {
        access = product_access
                     .emplace(*task.product_id,
                              Has(Permission::PRODUCT_ACCESS, task.product_id))
                     .first;
      }
      if (!access->second) continue;
    }
    AdministratorTaskInfo info;
    info.__set_normalInfo(ToTaskInfo(task));
    if (task.machine_id) info.__set_machineId(*task.machine_id);
    info.__set_statusConsumed(task.consumed);
    result.push_back(std::move(info));
  }
}

bool TaskHandler::cancelTask(const ::TaskToken& token) {
  std::optional<tasks::TaskRecord> task =
      ValueOrThrow(tasks::LoadTaskRecord(ConfigDb(), token));
  bool own_task = false;
  if (!MayAccess(task ? &*task : nullptr, Permission::PRODUCT_ADMIN,
                 &own_task)) {
    throw MakeRequestFailed(
        ErrorCode::UNAUTHORIZED,
        "The task does not exist, or you are not authorized to cancel the "
        "task. (Only the task's submitter, people with PRODUCT_ADMIN on the "
        "task's product, or a SUPERUSER can cancel the task.)");
  }
  if (!task) {
    throw MakeRequestFailed(ErrorCode::GENERAL, "The task does not exist.");
  }
  if (!tasks::CanBeCancelled(*task)) return false;
  tasks::AddComment("User requested cancellation.", User(), &*task);
  task->cancel_flag = true;
  ThrowIfFailed(tasks::UpdateTaskRecord(ConfigDb(), *task));
  return true;
}

void TaskHandler::createDummyTask(::TaskToken& result, int32_t timeout,
                                  bool should_fail) {
  if (!platform::GetEnv("TEST_WORKSPACE")) {
    throw MakeRequestFailed(
        ErrorCode::GENERAL,
        "createDummyTask() is only available in testing environments!");
  }
  tasks::TaskManager& manager = request_.server->tasks();
  result = ValueOrThrow(
      manager.AllocateTaskRecord("TaskService::DummyTask",
                                 "Dummy task for testing purposes", User(),
                                 std::nullopt),
      ErrorCode::GENERAL);
  ThrowIfFailed(
      manager.PushTask({.token = result,
                        .data_path = "",
                        .input = tasks::DummyTaskInput{
                            .timeout = timeout, .should_fail = should_fail}},
                       nullptr),
      ErrorCode::GENERAL);
}

}  // namespace cc::server::api
