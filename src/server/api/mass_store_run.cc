#include "server/api/mass_store_run.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <chrono>
#include <compare>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/report_server_types.h"
#include "api/thrift/shared_types.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "common/util.h"
#include "platform/filesystem.h"
#include "platform/ipc.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "report/parser/report_file.h"
#include "server/api/report_filter.h"
#include "server/api/request_failed.h"
#include "server/database/connection.h"
#include "server/database/db_cleanup.h"
#include "server/database/sql.h"
#include "server/metadata.h"
#include "server/product.h"
#include "server/server_context.h"
#include "server/tasks/task_manager.h"
#include "util/base64.h"
#include "util/compression.h"
#include "util/datetime.h"
#include "util/file.h"
#include "util/fnmatch.h"
#include "util/hash.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"
#include "util/zip.h"

namespace cc::server::api {
namespace {

using tasks::TaskError;
using tasks::TaskFailure;
using tasks::TaskResult;

constexpr util::Logger kLog("server");

// The largest ZIP a store may upload, decompressed (Python:
// decompressed_zip_max_size).
constexpr int64_t kMaxZipSize = int64_t{16} * 1024 * 1024 * 1024;

// The file of the task's input folder that holds its StoreOptions.
constexpr std::string_view kConfigurationFile = "store_configuration.json";

// The checker of reports whose checker has no row yet, and the checker of
// reports without one (Python: FakeChecker, UnknownChecker).
constexpr std::string_view kFakeName = "__FAKE__";
constexpr std::string_view kUnknownAnalyzer = "UNKNOWN";
constexpr std::string_view kUnknownChecker = "NOT FOUND";

// The message of the AttributeError that the reference fails with for a
// report annotation it does not accept (store.md §4.6).
constexpr std::string_view kAnnotationError =
    "type object 'ErrorCode' has no attribute 'REPORT_FORMAT'";

// The seconds since `start`.
double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                       start)
      .count();
}

// Logs a step of a store when it begins, and when the object is destroyed,
// that it ended, with its duration (Python: StepLog).
class StepLog {
 public:
  StepLog(std::string_view run_name, std::string_view message)
      : run_name_(run_name),
        message_(message),
        start_(std::chrono::steady_clock::now()) {
    kLog.Info(std::format("[{}] {}...", run_name_, message_));
  }

  StepLog(const StepLog&) = delete;
  StepLog& operator=(const StepLog&) = delete;

  ~StepLog() {
    kLog.Info(std::format("[{}] {}. Done. (Duration: {:.2f} sec)", run_name_,
                          message_, SecondsSince(start_)));
  }

 private:
  std::string run_name_;
  std::string message_;
  std::chrono::steady_clock::time_point start_;
};

// Returns the statement that reads the lock of a run, locking its row where
// the database can (Python: query(RunLock).with_for_update(nowait=True)).
std::string RunLockQuery(const db::Connection& connection) {
  std::string query =
      "SELECT locked_at, username FROM run_locks WHERE name = ?";
  if (connection.dialect() == db::Dialect::kPostgresql) {
    query += " FOR UPDATE NOWAIT";
  }
  return query;
}

// --- The store options file ---

// Returns `strings` as a JSON array in `doc`; null for nullopt.
yyjson_mut_val* JsonStrings(
    yyjson_mut_doc* doc,
    const std::optional<std::vector<std::string>>& strings) {
  if (!strings) return yyjson_mut_null(doc);
  yyjson_mut_val* array = yyjson_mut_arr(doc);
  for (const std::string& text : *strings) {
    yyjson_mut_arr_append(array, util::MakeJsonString(doc, text));
  }
  return array;
}

// Writes `options` to the file `path` (Python: the json.dump() of
// create_mass_store_task()).
Result<void> WriteStoreOptions(const std::string& path,
                               const StoreOptions& options) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, root, "client_version",
                         util::MakeJsonString(doc, options.client_version));
  yyjson_mut_obj_add_bool(doc, root, "force_overwrite", options.force);
  yyjson_mut_obj_add_val(doc, root, "path_prefixes_to_trim",
                         JsonStrings(doc, options.trim_path_prefixes));
  yyjson_mut_obj_add_val(doc, root, "run_name",
                         util::MakeJsonString(doc, options.run_name));
  yyjson_mut_obj_add_val(
      doc, root, "run_description",
      util::MakeJsonOptionalString(doc, options.description));
  yyjson_mut_obj_add_val(doc, root, "store_tag",
                         util::MakeJsonOptionalString(doc, options.tag));
  yyjson_mut_obj_add_val(doc, root, "user_name",
                         util::MakeJsonString(doc, options.user_name));
  return util::WriteFile(path, util::DumpJson(root));
}

// Reads the options that WriteStoreOptions() wrote to `path`.
Result<StoreOptions> ReadStoreOptions(const std::string& path) {
  const Result<std::string> text = util::ReadFile(path);
  if (!text.ok()) return Result<StoreOptions>(text.error());
  const Result<util::JsonDocument> document = util::ParseJson(text.value());
  if (!document.ok()) return Result<StoreOptions>(document.error());
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  StoreOptions options;
  options.run_name = util::JsonString(util::JsonGet(root, "run_name"));
  options.tag = util::JsonOptionalString(util::JsonGet(root, "store_tag"));
  options.client_version =
      util::JsonString(util::JsonGet(root, "client_version"));
  options.force = yyjson_get_bool(util::JsonGet(root, "force_overwrite"));
  yyjson_val* prefixes = util::JsonGet(root, "path_prefixes_to_trim");
  if (yyjson_is_arr(prefixes)) {
    std::vector<std::string>& list = options.trim_path_prefixes.emplace();
    size_t index = 0;
    size_t count = 0;
    yyjson_val* prefix = nullptr;
    yyjson_arr_foreach(prefixes, index, count, prefix) {
      list.push_back(util::JsonString(prefix));
    }
  }
  options.description =
      util::JsonOptionalString(util::JsonGet(root, "run_description"));
  options.user_name = util::JsonString(util::JsonGet(root, "user_name"));
  return Result<StoreOptions>(std::move(options));
}

// --- The API process: checking and preparing a store ---

// Fails if the product `product_id` reached its run limit, or the server's
// (`max_run_count`), and `run_name` is not one of its runs yet (Python:
// _check_run_limit()).
ApiResult<void> CheckRunLimit(const Request& request,
                              std::optional<int64_t> product_limit,
                              const std::string& run_name) {
  std::optional<int64_t> run_limit = request.server->config().MaxRunCount();
  if (product_limit.value_or(0) != 0) run_limit = product_limit;
  if (run_limit.value_or(0) == 0) return ApiResult<void>();
  kLog.Debug(std::format(
      "Checking the maximum number of allowed runs in '{}', which is {}.",
      request.product->endpoint(), *run_limit));
  Result<db::Connection*> db = request.product->Connection();
  if (!db.ok()) return ApiResult<void>(DatabaseError(db.error()));
  Result<std::optional<std::string>> existing = db::QueryValue(
      *db.value(), "SELECT id FROM runs WHERE name = ?", {run_name});
  if (!existing.ok()) return ApiResult<void>(DatabaseError(existing.error()));
  Result<std::optional<std::string>> count =
      db::QueryValue(*db.value(), "SELECT count(*) FROM runs", {});
  if (!count.ok()) return ApiResult<void>(DatabaseError(count.error()));
  const int64_t run_count =
      util::ParseInt(count.value().value_or("0")).value_or(0);
  if (existing.value() || run_count < *run_limit) return ApiResult<void>();
  return ApiResult<void>(ApiError{
      .code = ErrorCode::GENERAL,
      .message = std::format(
          "You reached the maximum number of allowed runs ({}/{})! Please "
          "remove at least {} run(s) before you try again!",
          run_count, *run_limit, run_count - *run_limit + 1)});
}

// Takes the lock of the run `run_name` for `user`: a new one, or one that
// expired; fails if someone else holds it (Python:
// RunLock.store_run_lock_in_db()).
ApiResult<void> StoreRunLock(db::Connection& db, const std::string& run_name,
                             const std::string& user) {
  auto fail = [&db](std::string message) {
    (void)db.Execute("ROLLBACK");
    return ApiResult<void>(
        ApiError{.code = ErrorCode::DATABASE, .message = std::move(message)});
  };
  const Result<void> begun = db.Execute("BEGIN");
  Result<db::QueryResult> lock =
      begun.ok() ? db.Query(RunLockQuery(db), {run_name})
                 : Result<db::QueryResult>(begun.error());
  if (!lock.ok()) {
    kLog.Error(std::format("Failed to get run lock for '{}': {}", run_name,
                           lock.error().message));
    return fail(
        "Someone is already storing to the same run. Please wait while the "
        "other storage is finished and try it again.");
  }
  const int64_t now = util::LocalNow();
  Result<db::QueryResult> written(db::QueryResult{});
  if (lock.value().rows.empty()) {
    written = db.Query(
        "INSERT INTO run_locks (name, locked_at, username) VALUES (?, ?, ?)",
        {run_name, util::FormatDatabaseTime(now), user});
    kLog.Debug(std::format("Acquiring 'run_lock' for '{}' on run '{}' ...",
                           user, run_name));
  } else {
    const db::Row& row = lock.value().rows.front();
    const int64_t expires =
        util::ParseDatabaseTime(row.Text(0)).value_or(0) +
        db::kRunLockTimeoutSeconds * util::kMicrosecondsPerSecond;
    if (now <= expires) {
      const std::string holder =
          row.Text(1).empty() ? "another user" : row.Text(1);
      const std::string when = util::FormatPythonTime(expires);
      kLog.Info(std::format(
          "Refusing to store into run '{}' as it is locked by {}. Lock will "
          "expire at '{}'.",
          run_name, holder, when));
      return fail(std::format(
          "The run named '{}' is being stored into by {}. If the other store "
          "operation has failed, this lock will expire at '{}'.",
          run_name, holder, when));
    }
    written = db.Query(
        "UPDATE run_locks SET locked_at = ?, username = ? WHERE name = ?",
        {util::FormatDatabaseTime(now), user, run_name});
    kLog.Debug(std::format(
        "Reusing existing, stale 'run_lock' record on run '{}' ...",
        run_name));
  }
  // Another store may have taken the lock in the meantime.
  if (!written.ok() || !db.Execute("COMMIT").ok()) {
    kLog.Info(std::format(
        "Run '{}' got locked while current transaction tried to acquire a "
        "lock. Considering run as locked.",
        run_name));
    return fail(std::format(
        "The run named '{}' is being stored into by another user.",
        run_name));
  }
  kLog.Debug(std::format("Successfully acquired 'run_lock' for '{}' on run "
                         "'{}'.",
                         user, run_name));
  return ApiResult<void>();
}

// Returns the error that rejects the store of `run_name` because its ZIP
// is too large, after logging it.
ApiError RejectZip(const std::string& run_name) {
  std::string message = std::format(
      "Rejected storage of run '{}', decompressed ZIP file is too large!",
      run_name);
  kLog.Info(message);
  return ApiError{.code = ErrorCode::IOERROR, .message = std::move(message)};
}

// Writes the files of the Base64 encoded, zlib-compressed ZIP `zip_base64`
// into `output_dir` and returns the size of the decompressed ZIP; 0 if
// `zip_base64` is empty (Python: unzip()).
ApiResult<int64_t> Unzip(const std::string& run_name,
                         const std::string& zip_base64,
                         const std::string& output_dir) {
  if (zip_base64.empty()) return ApiResult<int64_t>(0);
  const std::optional<std::string> compressed =
      util::Base64Decode(zip_base64);
  if (!compressed) {
    return ApiResult<int64_t>(
        ApiError{.code = ErrorCode::GENERAL, .message = "Incorrect padding"});
  }
  const std::optional<std::string> zip_file =
      platform::MakeTempFile(output_dir, ".zip");
  if (!zip_file) {
    return ApiResult<int64_t>(ApiError{
        .message = std::format("Couldn't create a file in '{}'.",
                               output_dir)});
  }
  kLog.Debug(std::format("Decompressing input massStoreRun() ZIP to '{}' ...",
                         *zip_file));
  Result<util::ZlibPart> zip = util::ZlibDecompressPart(
      *compressed, static_cast<size_t>(kMaxZipSize));
  // The temporary ZIP is deleted however this ends.
  const auto remove_zip = [&zip_file] {
    (void)platform::RemoveTree(*zip_file);
  };
  Result<void> written = zip.ok() ? util::WriteFile(*zip_file, zip.value().data)
                                  : Result<void>(zip.error());
  if (!written.ok()) {
    remove_zip();
    return ApiResult<int64_t>(ApiError{.message = written.error().message});
  }
  if (zip.value().limited) {
    remove_zip();
    return ApiResult<int64_t>(RejectZip(run_name));
  }
  const int64_t size = static_cast<int64_t>(zip.value().data.size());
  zip.value().data.clear();
  StepLog step(run_name, "Extract massStoreRun() ZIP contents");
  const Result<std::vector<util::ZipEntry>> entries =
      util::ListZipEntries(*zip_file);
  if (!entries.ok()) {
    remove_zip();
    return ApiResult<int64_t>(ApiError{.message = entries.error().message});
  }
  kLog.Debug(std::format("Extracting massStoreRun() ZIP '{}' to '{}' ...",
                         *zip_file, output_dir));
  int64_t unzipped_size = 0;
  for (const util::ZipEntry& entry : entries.value()) {
    unzipped_size += entry.size;
  }
  ApiResult<int64_t> result(size);
  if (unzipped_size > kMaxZipSize) {
    result = ApiResult<int64_t>(RejectZip(run_name));
  } else if (Result<void> extracted = util::ExtractZip(*zip_file, output_dir);
             !extracted.ok()) {
    result = ApiResult<int64_t>(ApiError{.message = extracted.error().message});
  }
  if (!result.ok()) kLog.Error("Failed to extract received ZIP.");
  remove_zip();
  return result;
}

// Creates the task of the store of `zip_base64` with `options` into the
// product of `request`, with its input data, and returns it (Python:
// create_mass_store_task()). `start` is when the request arrived.
ApiResult<tasks::QueuedTask> CreateMassStoreTask(
    const Request& request, const StoreOptions& options,
    const std::string& zip_base64, bool asynchronous,
    std::chrono::steady_clock::time_point start) {
  using TaskApiResult = ApiResult<tasks::QueuedTask>;
  tasks::TaskManager& manager = request.server->tasks();
  const Result<std::string> token = manager.AllocateTaskRecord(
      asynchronous ? "report_server::massStoreRunAsynchronous()"
                   : "report_server::massStoreRun()",
      std::format("{}tore of results to '{}' - '{}'",
                  asynchronous ? "S" : "Legacy s",
                  request.product->endpoint(), options.run_name),
      options.user_name, request.product->id());
  if (!token.ok()) {
    return TaskApiResult(ApiError{.message = token.error().message});
  }
  const Result<std::string> data_path = manager.CreateTaskData(token.value());
  if (!data_path.ok()) {
    return TaskApiResult(ApiError{.message = data_path.error().message});
  }
  const std::string extract_dir =
      util::pypath::Join({data_path.value(), "store_zip"});
  (void)platform::EnsureDirs(extract_dir);
  ApiResult<int64_t> zip_size(0);
  {
    StepLog step(options.run_name,
                 "Save massStoreRun() ZIP data to server storage");
    zip_size = Unzip(options.run_name, zip_base64, extract_dir);
    if (zip_size.ok() && zip_size.value() == 0) {
      zip_size = ApiResult<int64_t>(
          ApiError{.code = ErrorCode::GENERAL,
                   .message = "The uploaded ZIP file is empty!"});
    }
  }
  if (!zip_size.ok()) {
    kLog.Error("Failed to extract massStoreRunAsynchronous() ZIP!");
    return TaskApiResult(zip_size.error());
  }
  const double preparation_seconds = SecondsSince(start);
  if (Result<void> written = WriteStoreOptions(
          util::pypath::Join({data_path.value(), kConfigurationFile}),
          options);
      !written.ok()) {
    kLog.Error(
        "Failed to write massStoreRunAsynchronous() configuration!");
    return TaskApiResult(ApiError{.message = written.error().message});
  }
  tasks::QueuedTask task = {
      .token = token.value(),
      .data_path = data_path.value(),
      .input = tasks::StoreTaskInput{
          .product_id = request.product->id(),
          .zip_size = zip_size.value(),
          .preparation_seconds = preparation_seconds}};
  if (!asynchronous) {
    if (Result<void> commented = manager.AddComment(
            task.token,
            "WARNING!\nExecuting a legacy 'massStoreRun()' API call!",
            "SYSTEM");
        !commented.ok()) {
      return TaskApiResult(ApiError{.message = commented.error().message});
    }
  }
  return TaskApiResult(std::move(task));
}

// --- The task worker: storing the results ---

// An analyzer and a checker name, which identify a row of the checkers
// table. Ordered by analyzer, then by checker; equal if both are equal (the
// defaulted <=> also declares ==).
struct CheckerName {
  std::string analyzer;
  std::string checker;

  friend auto operator<=>(const CheckerName& a,
                          const CheckerName& b) = default;
};

// Returns the checker row of `report` (Python: checker_name_for_report()).
CheckerName CheckerOf(const report::Report& report) {
  return {.analyzer = report.analyzer_name.value_or("").empty()
                          ? std::string(kUnknownAnalyzer)
                          : *report.analyzer_name,
          .checker = report.checker_name.empty()
                         ? std::string(kUnknownChecker)
                         : report.checker_name};
}

// Returns the value of the API's Severity named `name`; nullopt if there is
// none (Python: Severity._NAMES_TO_VALUES[name]).
std::optional<int64_t> SeverityValue(const std::string& name) {
  for (const auto& [value, severity_name] : _Severity_VALUES_TO_NAMES) {
    if (name == severity_name) return value;
  }
  return std::nullopt;
}

// Returns `text` as Python's str(datetime.fromisoformat(text)) writes it;
// nullopt if fromisoformat() rejects it. A date "YYYY-MM-DD" may be
// followed by "T" or " " and a time "HH[:MM[:SS[.ffffff]]]", and that by a
// time zone "Z" or "+HH:MM"; the separators may be left out.
std::optional<std::string> NormalizeIsoTime(std::string_view text) {
  // Reads `count` digits from the start of `text`; -1 if they are not.
  auto digits = [&text](size_t count) {
    if (text.size() < count) return -1;
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
      if (text[i] < '0' || text[i] > '9') return -1;
      value = value * 10 + (text[i] - '0');
    }
    text.remove_prefix(count);
    return value;
  };
  // Drops `c` from the start of `text`; returns whether it was there.
  auto skip = [&text](char c) {
    if (text.empty() || text.front() != c) return false;
    text.remove_prefix(1);
    return true;
  };
  const int year = digits(4);
  skip('-');
  const int month = digits(2);
  skip('-');
  const int day = digits(2);
  if (year < 0 || month < 0 || day < 0 ||
      !std::chrono::year_month_day(std::chrono::year(year),
                                   std::chrono::month(month),
                                   std::chrono::day(day))
           .ok()) {
    return std::nullopt;
  }
  int hour = 0;
  int minute = 0;
  int second = 0;
  std::string fraction;
  std::string zone;
  if (!text.empty()) {
    if (!skip('T') && !skip(' ')) return std::nullopt;
    hour = digits(2);
    if (!text.empty() && text.front() != 'Z' && text.front() != '+' &&
        text.front() != '-') {
      skip(':');
      minute = digits(2);
      if (!text.empty() && (text.front() == ':' ||
                            (text.front() >= '0' && text.front() <= '9'))) {
        skip(':');
        second = digits(2);
        if (skip('.') || skip(',')) {
          while (!text.empty() && text.front() >= '0' && text.front() <= '9') {
            if (fraction.size() < 6) fraction += text.front();
            text.remove_prefix(1);
          }
          if (fraction.empty()) return std::nullopt;
          fraction.resize(6, '0');
        }
      }
    }
    if (skip('Z')) {
      zone = "+00:00";
    } else if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
      const char sign = text.front();
      text.remove_prefix(1);
      const int zone_hour = digits(2);
      skip(':');
      const int zone_minute = digits(2);
      if (zone_hour < 0 || zone_hour > 23 || zone_minute < 0 ||
          zone_minute > 59) {
        return std::nullopt;
      }
      zone = std::format("{}{:02}:{:02}", sign, zone_hour, zone_minute);
    }
  }
  if (!text.empty() || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return std::nullopt;
  }
  if (fraction == "000000") fraction.clear();
  return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}{}{}", year, month,
                     day, hour, minute, second,
                     fraction.empty() ? "" : "." + fraction, zone);
}

// Returns the error of a store whose source code comments were invalid
// (Python: the end of MassStoreRun.store()): each message of `comments` is
// split into the columns of a table, which in the reference always fails.
TaskError WrongCommentsError(const std::vector<std::string>& comments) {
  std::vector<std::vector<std::string>> rows;
  for (const std::string& comment : comments) {
    rows.push_back(util::Split(comment, "|", 3));
    if (rows.back().size() != 3) {
      return TaskError{.message = std::format(
                           "Row has incorrect number of values, (actual) "
                           "{}!=3 (expected)",
                           rows.back().size())};
    }
  }
  return TaskError{.message = std::format(
                       "One or more source files contained invalid source "
                       "code comments! Failed to set review statuses.\n\n{}",
                       util::twodim::ToString(
                           "table", {"File", "Line", "Checker name"}, rows))};
}

// A report that the run had before the store (Python: the DBReports of
// __store_reports()).
struct OldReport {
  int64_t id = 0;
  std::string detection_status;
  int64_t detected_at = 0;
  std::string review_status;
  std::optional<int64_t> fixed_at;
  std::string checker_name;
};

// A report that the store added, and its row.
struct AddedReport {
  int64_t id;
  report::Report report;
};

// A report that the store added with the __FAKE__ checker, because its
// checker had no row yet.
struct FakeCheckerReport {
  CheckerName checker;
  int64_t id;
};

// The run that the store writes into.
struct StoredRun {
  int64_t id = 0;
  bool updated = false;  // Whether the run existed before the store.
};

// Stores the unzipped results of one store task into the product database
// (Python: MassStoreRun). Used once, by RunMassStoreTask().
class MassStoreRun {
 public:
  // Keeps `context`, `product` and `db`, which must outlive the object.
  // `zip_dir` is where the ZIP of the store was unpacked.
  MassStoreRun(ServerContext* context, std::string token, Product* product,
               db::Connection* db, StoreOptions options, std::string zip_dir,
               int64_t report_limit)
      : context_(context),
        token_(std::move(token)),
        product_(product),
        db_(db),
        options_(std::move(options)),
        trim_patterns_(report::PathPrefixPatterns(
            options_.trim_path_prefixes.value_or(
                std::vector<std::string>()))),
        zip_dir_(std::move(zip_dir)),
        report_limit_(report_limit) {}

  MassStoreRun(const MassStoreRun&) = delete;
  MassStoreRun& operator=(const MassStoreRun&) = delete;

  // Stores the results; `zip_size` is the size of the decompressed ZIP and
  // `preparation_seconds` the time the API process spent on the task
  // (Python: store()).
  TaskResult<void> Store(int64_t zip_size, double preparation_seconds);

 private:
  // Fails with a cancellation if the task should stop.
  TaskResult<void> CancelIfRequested() {
    return context_->tasks().CancelIfRequested(token_);
  }

  // Runs `sql` with `params` on the product database.
  TaskResult<db::QueryResult> Query(std::string_view sql,
                                    const std::vector<db::Param>& params);

  // Returns the id in the first column of the first row of `sql`; nullopt
  // if there is no row.
  TaskResult<std::optional<int64_t>> QueryId(
      std::string_view sql, const std::vector<db::Param>& params);

  // Runs `body` in a transaction, which is rolled back if `body` or the
  // commit fails (Python: a DBSession).
  TaskResult<void> InTransaction(
      const std::function<TaskResult<void>()>& body);

  // Everything of Store() that happens while the run is locked.
  TaskResult<void> StoreLocked(int64_t zip_size, double preparation_seconds,
                               std::chrono::steady_clock::time_point start);

  // Stores the source files of the ZIP, `hashes` (path to content hash),
  // and returns the id of each by its trimmed path (Python:
  // __store_source_files()).
  TaskResult<std::map<std::string, int64_t>> StoreSourceFiles(
      const std::string& source_root,
      const cc::api::OrderedMap<std::string, std::string>& hashes);

  // Stores the content of the file `path` unless a content with its hash is
  // stored; returns the hash, computed if `hash` is nullopt (Python:
  // __add_file_content()).
  TaskResult<std::string> AddFileContent(const std::string& path,
                                         std::optional<std::string> hash);

  // Returns the id of the file record of `path` with `hash`, added if it is
  // missing; nullopt if it cannot be added (Python: add_file_record()).
  TaskResult<std::optional<int64_t>> AddFileRecord(const std::string& path,
                                                   const std::string& hash);

  // Adds the new file `file_id` at `path` to the source components that
  // match it (Python: assign_file_to_source_components()).
  TaskResult<void> AssignFileToSourceComponents(int64_t file_id,
                                                const std::string& path);

  // Stores the blame information of the files under `blame_root`, where it
  // is missing (Python: __add_blame_info()).
  TaskResult<void> AddBlameInfo(
      const std::string& blame_root,
      const cc::api::OrderedMap<std::string, std::string>& hashes);

  // Makes sure that `checkers` and the two pseudo checkers have rows,
  // retrying on contention (Python: __store_checker_identifiers()).
  TaskResult<void> StoreCheckerIdentifiers(std::set<CheckerName> checkers);

  // Returns the id of the checker row of `name`; nullopt if there is none
  // (Python: __get_checker()).
  TaskResult<std::optional<int64_t>> CheckerId(const CheckerName& name);

  // Reads the lock of the run in the current transaction; fails if there
  // is none (Python: RunLock.__enter__()).
  TaskResult<void> SelectRunLock();

  // Adds the run or updates it, with a history entry, its statistics and
  // analysis info (Python: __add_or_update_run()).
  TaskResult<StoredRun> AddOrUpdateRun(int64_t run_history_time);

  // Returns the id of a new run (Python: Run()).
  TaskResult<int64_t> InsertRun();

  // Stores the analyzer statistics of the metadata files for the history
  // entry `history_id` (Python: __store_analysis_statistics()).
  TaskResult<void> StoreAnalysisStatistics(int64_t history_id);

  // Stores the analysis info of the metadata files for the history entry
  // `history_id` (Python: __store_analysis_info()).
  TaskResult<void> StoreAnalysisInfo(int64_t history_id);

  // Returns the id of the checker set of `enabled` and `disabled` checker
  // ids, created if it is new.
  TaskResult<int64_t> CheckerSetId(std::vector<int64_t> enabled,
                                   std::vector<int64_t> disabled);

  // Stores the files of the conf/ folder of the report folder `folder` for
  // the analysis info `analysis_info_id` (Python:
  // __store_analysis_info_files()).
  TaskResult<void> StoreAnalysisInfoFiles(int64_t analysis_info_id,
                                          const std::string& folder);

  // Stores the reports of the result files under `report_dir` into the run
  // `run_id` (Python: __store_reports()).
  TaskResult<void> StoreReports(const std::string& report_dir,
                                const std::string& source_root,
                                int64_t run_id,
                                const std::map<std::string, int64_t>& file_ids,
                                int64_t run_history_time);

  // Returns the reports of the run `run_id`, grouped by report hash, in the
  // order of their ids.
  TaskResult<cc::api::OrderedMap<std::string, std::vector<OldReport>>>
  LoadOldReports(int64_t run_id);

  // Stores the reports of the result file `path` (Python:
  // __process_report_file()).
  TaskResult<void> ProcessReportFile(
      const std::string& path, int64_t run_id,
      const std::map<std::string, int64_t>& file_ids, int64_t run_history_time,
      const common::SkipListHandler& skip_handler,
      common::ReviewStatusHandler& review_status_handler,
      const cc::api::OrderedMap<std::string, std::vector<OldReport>>&
          old_reports);

  // Inserts the report `report` (Python: __add_report()).
  TaskResult<void> AddReport(const report::Report& report,
                             const std::string& path_hash, int64_t run_id,
                             int64_t file_id,
                             const report::ReviewStatus& review_status,
                             std::string_view detection_status,
                             int64_t detected_at, int64_t run_history_time,
                             std::optional<int64_t> analysis_info_id,
                             std::optional<int64_t> fixed_at);

  // Stores the bug paths, the files they use and the annotations of the
  // added reports (Python: __add_report_context()).
  TaskResult<void> AddReportContext(
      const std::map<std::string, int64_t>& file_ids);

  // Checks and stores the annotations of the report `report_id` (Python:
  // __validate_and_add_report_annotations()).
  TaskResult<void> AddReportAnnotations(
      int64_t report_id,
      const std::vector<std::pair<std::string, std::string>>& annotations);

  // Gives the added reports the status of their review status rule
  // (Python: the reports_to_rs_rules part of __store_reports()).
  TaskResult<void> ApplyReviewStatusRules(
      int64_t run_id,
      const cc::api::OrderedMap<std::string, std::vector<OldReport>>&
          old_reports,
      int64_t run_history_time);

  // Deletes the old reports that were stored again and sets the detection
  // status of the others (Python: the end of __store_reports()).
  TaskResult<void> UpdateOldReports(
      const cc::api::OrderedMap<std::string, std::vector<OldReport>>&
          old_reports,
      const std::set<std::string>& enabled_checkers,
      const std::set<std::string>& disabled_checkers,
      int64_t run_history_time);

  // Points the reports stored with the __FAKE__ checker to their real
  // checkers (Python: __realise_fake_checkers()).
  TaskResult<void> RealiseFakeCheckers();

  // Sets the duration of the run `run_id` (Python: finish_checker_run()).
  void FinishCheckerRun(int64_t run_id);

  // Removes the lock of the run (Python: drop_run_lock_from_db()).
  TaskResult<void> DropRunLock();

  ServerContext* context_;
  std::string token_;
  Product* product_;
  db::Connection* db_;
  StoreOptions options_;
  std::vector<util::Regex> trim_patterns_;
  std::string zip_dir_;
  int64_t report_limit_;
  // The metadata of each folder under reports/, in os.walk() order.
  cc::api::OrderedMap<std::string, MetadataInfo> metadata_;
  // The id of the analysis info of each report folder.
  std::map<std::string, int64_t> analysis_info_;
  std::map<CheckerName, int64_t> checker_ids_;
  int64_t duration_ = 0;      // Seconds, the sum of the metadata.
  int64_t report_count_ = 0;  // All reports read, also the skipped ones.
  std::vector<std::string> wrong_source_code_comments_;
  std::set<std::string> added_path_hashes_;
  // The review status of the added reports, by report hash.
  std::map<std::string, std::string> new_report_hashes_;
  std::set<std::string> report_checkers_;
  std::vector<AddedReport> added_reports_;
  // By path hash.
  std::map<std::string, FakeCheckerReport> fake_checker_reports_;
};

TaskResult<db::QueryResult> MassStoreRun::Query(
    std::string_view sql, const std::vector<db::Param>& params) {
  Result<db::QueryResult> rows = db_->Query(sql, params);
  if (!rows.ok()) return TaskResult<db::QueryResult>(TaskFailure(rows.error()));
  return TaskResult<db::QueryResult>(std::move(rows).value());
}

TaskResult<std::optional<int64_t>> MassStoreRun::QueryId(
    std::string_view sql, const std::vector<db::Param>& params) {
  TaskResult<db::QueryResult> rows = Query(sql, params);
  if (!rows.ok()) return TaskResult<std::optional<int64_t>>(rows.error());
  if (rows.value().rows.empty()) {
    return TaskResult<std::optional<int64_t>>(std::nullopt);
  }
  return TaskResult<std::optional<int64_t>>(rows.value().rows.front().Int(0));
}

TaskResult<void> MassStoreRun::InTransaction(
    const std::function<TaskResult<void>()>& body) {
  if (Result<void> begun = db_->Execute("BEGIN"); !begun.ok()) {
    return TaskResult<void>(TaskFailure(begun.error()));
  }
  TaskResult<void> result = body();
  if (result.ok()) {
    const Result<void> committed = db_->Execute("COMMIT");
    if (committed.ok()) return result;
    result = TaskResult<void>(TaskFailure(committed.error()));
  }
  (void)db_->Execute("ROLLBACK");
  return result;
}

TaskResult<std::map<std::string, int64_t>> MassStoreRun::StoreSourceFiles(
    const std::string& source_root,
    const cc::api::OrderedMap<std::string, std::string>& hashes) {
  using FileIds = std::map<std::string, int64_t>;
  FileIds file_ids;
  for (const auto& [file_name, hash] : hashes) {
    if (TaskResult<void> go_on = CancelIfRequested(); !go_on.ok()) {
      return TaskResult<FileIds>(go_on.error());
    }
    const std::string source_path =
        common::PathForFakeRoot(file_name, source_root);
    kLog.Debug(std::format("Storing source file: {}", source_path));
    const std::string trimmed =
        report::TrimPathPrefixes(file_name, trim_patterns_);
    // A file not in the ZIP has its content stored already.
    const bool in_zip = util::pypath::IsFile(source_path);
    if (!in_zip) {
      kLog.Debug(std::format("{} not found or already stored.", trimmed));
    } else if (TaskResult<std::string> added =
                   AddFileContent(source_path, hash);
               !added.ok()) {
      return TaskResult<FileIds>(added.error());
    }
    const TaskResult<std::optional<int64_t>> id = AddFileRecord(trimmed, hash);
    if (!id.ok()) return TaskResult<FileIds>(id.error());
    if (!id.value()) {
      kLog.Error(std::format(
          "File ID for {} is not found in the DB with content hash {}. "
          "Missing from ZIP?",
          source_path, hash));
      continue;
    }
    file_ids[trimmed] = *id.value();
    if (!in_zip) kLog.Debug(std::format("{} fileid found", *id.value()));
  }
  return TaskResult<FileIds>(std::move(file_ids));
}

TaskResult<std::string> MassStoreRun::AddFileContent(
    const std::string& path, std::optional<std::string> hash) {
  std::optional<std::string> content;
  if (!hash) {
    Result<std::string> read = util::ReadFile(path);
    if (!read.ok()) return TaskResult<std::string>(TaskFailure(read.error()));
    content = std::move(read).value();
    hash = util::HexDigest("sha256", *content);
  }
  TaskResult<db::QueryResult> stored = Query(
      "SELECT content_hash FROM file_contents WHERE content_hash = ?",
      {*hash});
  if (!stored.ok()) return TaskResult<std::string>(stored.error());
  if (!stored.value().rows.empty()) return TaskResult<std::string>(*hash);
  if (!content) {
    Result<std::string> read = util::ReadFile(path);
    if (!read.ok()) return TaskResult<std::string>(TaskFailure(read.error()));
    content = std::move(read).value();
  }
  // Another store may add the same content at the same time.
  TaskResult<db::QueryResult> inserted = Query(
      "INSERT INTO file_contents (content_hash, content, blame_info) "
      "VALUES (?, ?, NULL) ON CONFLICT DO NOTHING",
      {*hash, db::Bytes{util::ZlibCompress(*content)}});
  if (!inserted.ok()) return TaskResult<std::string>(inserted.error());
  return TaskResult<std::string>(*hash);
}

TaskResult<std::optional<int64_t>> MassStoreRun::AddFileRecord(
    const std::string& path, const std::string& hash) {
  using Id = TaskResult<std::optional<int64_t>>;
  constexpr std::string_view kSelect =
      "SELECT id FROM files WHERE content_hash = ? AND filepath = ?";
  Id existing = QueryId(kSelect, {hash, path});
  if (!existing.ok() || existing.value()) return existing;
  std::optional<int64_t> added;
  // The insert of a file whose content is missing fails at the commit,
  // because of the foreign key.
  const TaskResult<void> done = InTransaction([&]() {
    Id id = QueryId(
        "INSERT INTO files (filepath, filename, content_hash) VALUES (?, ?, "
        "?) ON CONFLICT (filepath, content_hash) DO NOTHING RETURNING id",
        {path, util::pypath::Basename(path), hash});
    if (!id.ok()) return TaskResult<void>(id.error());
    added = id.value();
    return added ? AssignFileToSourceComponents(*added, path)
                 : TaskResult<void>();
  });
  if (done.ok() && added) return Id(added);
  // Another store may have added the same file in the meantime.
  if (!done.ok()) kLog.Error(done.error().message);
  return QueryId(kSelect, {hash, path});
}

TaskResult<void> MassStoreRun::AssignFileToSourceComponents(
    int64_t file_id, const std::string& path) {
  TaskResult<db::QueryResult> components =
      Query("SELECT name, value FROM source_components", {});
  if (!components.ok()) return TaskResult<void>(components.error());
  for (const db::Row& component : components.value().rows) {
    const auto [include, skip] = ParseComponentValues(component.Blob(1));
    // A component without patterns matches nothing; one with only skip
    // patterns matches every other file.
    if (include.empty() && skip.empty()) continue;
    auto matches = [&path](const std::string& pattern) {
      return util::FnMatch(path, pattern);
    };
    if ((include.empty() || std::ranges::any_of(include, matches)) &&
        !std::ranges::any_of(skip, matches)) {
      TaskResult<db::QueryResult> added = Query(
          "INSERT INTO source_component_files (source_component_name, "
          "file_id) VALUES (?, ?)",
          {component.Text(0), file_id});
      if (!added.ok()) return TaskResult<void>(added.error());
    }
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::AddBlameInfo(
    const std::string& blame_root,
    const cc::api::OrderedMap<std::string, std::string>& hashes) {
  return InTransaction([&]() {
    for (const std::string& blame_file : platform::WalkFiles(blame_root)) {
      if (TaskResult<void> go_on = CancelIfRequested(); !go_on.ok()) {
        return go_on;
      }
      const std::string file_path =
          "/" + util::pypath::Relpath(blame_file, blame_root);
      // Python: get_blame_file_data().
      std::optional<std::string> blame_info;
      std::optional<std::string> remote_url;
      std::optional<std::string> tracking_branch;
      const std::optional<util::JsonDocument> data =
          util::pypath::IsFile(blame_file) ? common::LoadJson(blame_file)
                                           : std::nullopt;
      yyjson_val* root = data ? yyjson_doc_get_root(data->get()) : nullptr;
      if (util::JsonTruthy(root)) {
        remote_url =
            util::JsonOptionalString(util::JsonGet(root, "remote_url"));
        tracking_branch =
            util::JsonOptionalString(util::JsonGet(root, "tracking_branch"));
        const util::MutableJsonDocument copy = util::NewJsonDocument();
        yyjson_mut_val* info = yyjson_val_mut_copy(copy.get(), root);
        yyjson_mut_obj_remove_key(info, "remote_url");
        yyjson_mut_obj_remove_key(info, "tracking_branch");
        if (yyjson_mut_obj_size(info) > 0) {
          blame_info = util::ZlibCompress(util::DumpJson(info));
        }
      }
      if (const auto hash = hashes.find(file_path); hash != hashes.end()) {
        TaskResult<db::QueryResult> updated = Query(
            "UPDATE file_contents SET blame_info = ? WHERE blame_info IS NULL "
            "AND content_hash = ?",
            {blame_info ? db::Param(db::Bytes{*blame_info})
                        : db::Param(nullptr),
             hash->second});
        if (!updated.ok()) return TaskResult<void>(updated.error());
      }
      TaskResult<db::QueryResult> updated = Query(
          "UPDATE files SET remote_url = ?, tracking_branch = ? WHERE "
          "remote_url IS NULL AND filepath = ?",
          {db::OptionalText(remote_url), db::OptionalText(tracking_branch),
           file_path});
      if (!updated.ok()) return TaskResult<void>(updated.error());
    }
    return TaskResult<void>();
  });
}

TaskResult<void> MassStoreRun::StoreCheckerIdentifiers(
    std::set<CheckerName> checkers) {
  checkers.insert({std::string(kFakeName), std::string(kFakeName)});
  checkers.insert(
      {std::string(kUnknownAnalyzer), std::string(kUnknownChecker)});
  std::chrono::seconds wait_time(30);
  for (int tries = 1; tries <= 3; ++tries) {
    kLog.Debug(std::format("[{}] Begin attempt {}...", options_.run_name,
                           tries));
    // A checker whose severity is not an API Severity fails at once.
    bool bad_severity = false;
    const TaskResult<void> stored = InTransaction([&]() {
      TaskResult<db::QueryResult> known =
          Query("SELECT analyzer_name, checker_name FROM checkers", {});
      if (!known.ok()) return TaskResult<void>(known.error());
      std::set<CheckerName> missing = checkers;
      for (const db::Row& row : known.value().rows) {
        missing.erase({row.Text(0), row.Text(1)});
      }
      for (const CheckerName& name : missing) {
        const std::string severity =
            context_->package_data().checker_labels.UniqueLabelOfChecker(
                name.checker, "severity", std::nullopt);
        const std::optional<int64_t> value = SeverityValue(severity);
        if (!value) {
          bad_severity = true;
          return TaskResult<void>(TaskFailure(util::KeyError(severity)));
        }
        TaskResult<db::QueryResult> added = Query(
            "INSERT INTO checkers (analyzer_name, checker_name, severity) "
            "VALUES (?, ?, ?)",
            {name.analyzer, name.checker, *value});
        if (!added.ok()) return TaskResult<void>(added.error());
        kLog.Debug(std::format(
            "Acquiring ID for checker '{}/{}' for the first time.",
            name.analyzer, name.checker));
      }
      return TaskResult<void>();
    });
    if (stored.ok()) return stored;
    if (bad_severity) {
      kLog.Error(std::format(
          "Failed to store checkers due to some other error: {}",
          stored.error().message));
      return stored;
    }
    kLog.Error(std::format(
        "Storing checkers of run '{}' failed: {}.\nWaiting {} before trying "
        "again...",
        options_.run_name, stored.error().message,
        // Python's str(timedelta) of the wait, which is under an hour.
        std::format("0:{:02}:{:02}", wait_time.count() / 60,
                    wait_time.count() % 60)));
    std::this_thread::sleep_for(wait_time);
    wait_time *= 2;
  }
  return TaskResult<void>(TaskError{
      .message = "Storing the names of the checkers in the run failed due to "
                 "excessive contention!"});
}

TaskResult<std::optional<int64_t>> MassStoreRun::CheckerId(
    const CheckerName& name) {
  using Id = TaskResult<std::optional<int64_t>>;
  if (const auto cached = checker_ids_.find(name);
      cached != checker_ids_.end()) {
    return Id(cached->second);
  }
  Id id = QueryId(
      "SELECT id FROM checkers WHERE analyzer_name = ? AND checker_name = ?",
      {name.analyzer, name.checker});
  if (id.ok() && id.value()) checker_ids_[name] = *id.value();
  return id;
}

TaskResult<void> MassStoreRun::SelectRunLock() {
  TaskResult<db::QueryResult> lock =
      Query(RunLockQuery(*db_), {options_.run_name});
  if (!lock.ok()) return TaskResult<void>(lock.error());
  if (lock.value().rows.empty()) {
    return TaskResult<void>(
        TaskError{.message = "No row was found when one was required"});
  }
  kLog.Debug(std::format(
      "Acquired exclusive lock for run '{}' that was originally locked at "
      "'{}'.",
      options_.run_name,
      util::DatabaseTimeToPython(lock.value().rows.front().Text(0))));
  return TaskResult<void>();
}

TaskResult<int64_t> MassStoreRun::InsertRun() {
  TaskResult<std::optional<int64_t>> id = QueryId(
      "INSERT INTO runs (date, duration, name, version, can_delete) VALUES "
      "(?, -1, ?, ?, true) RETURNING id",
      {util::FormatDatabaseTime(util::LocalNow()), options_.run_name,
       options_.client_version.empty() ? db::Param(nullptr)
                                       : db::Param(options_.client_version)});
  if (!id.ok()) return TaskResult<int64_t>(id.error());
  return TaskResult<int64_t>(id.value().value_or(0));
}

TaskResult<StoredRun> MassStoreRun::AddOrUpdateRun(int64_t run_history_time) {
  kLog.Debug(std::format("Adding run '{}'...", options_.run_name));
  TaskResult<db::QueryResult> existing = Query(
      "SELECT id, can_delete FROM runs WHERE name = ?", {options_.run_name});
  if (!existing.ok()) return TaskResult<StoredRun>(existing.error());
  StoredRun run = {.id = 0, .updated = !existing.value().rows.empty()};
  TaskResult<int64_t> run_id(0);
  if (run.updated && options_.force) {
    const db::Row& row = existing.value().rows.front();
    if (!row.Bool(1)) {
      // Deletion is already in progress.
      const std::string message = std::format("Can't delete {}", row.Int(0));
      kLog.Debug(message);
      return TaskResult<StoredRun>(TaskError{.message = message});
    }
    kLog.Info("Removing previous analysis results...");
    TaskResult<db::QueryResult> removed =
        Query("DELETE FROM runs WHERE id = ?", {row.Int(0)});
    if (!removed.ok()) return TaskResult<StoredRun>(removed.error());
    run_id = InsertRun();
  } else if (run.updated) {
    run_id = TaskResult<int64_t>(existing.value().rows.front().Int(0));
    TaskResult<db::QueryResult> updated = Query(
        "UPDATE runs SET date = ?, duration = -1 WHERE id = ?",
        {util::FormatDatabaseTime(util::LocalNow()), run_id.value()});
    if (!updated.ok()) return TaskResult<StoredRun>(updated.error());
  } else {
    run_id = InsertRun();
  }
  if (!run_id.ok()) return TaskResult<StoredRun>(run_id.error());
  run.id = run_id.value();

  kLog.Debug("Adding run history.");
  if (options_.tag) {
    TaskResult<db::QueryResult> untagged = Query(
        "UPDATE run_histories SET version_tag = NULL WHERE run_id = ? AND "
        "version_tag = ?",
        {run.id, *options_.tag});
    if (!untagged.ok()) return TaskResult<StoredRun>(untagged.error());
  }
  std::set<std::string> cc_versions;
  for (const auto& [folder, metadata] : metadata_) {
    if (metadata.cc_version) cc_versions.insert(*metadata.cc_version);
  }
  const std::optional<std::string> cc_version =
      cc_versions.empty()
          ? std::nullopt
          : std::optional<std::string>(util::Join(
                std::vector<std::string>(cc_versions.begin(),
                                         cc_versions.end()),
                "; "));
  TaskResult<std::optional<int64_t>> history = QueryId(
      "INSERT INTO run_histories (run_id, version_tag, \"user\", time, "
      "cc_version, description) VALUES (?, ?, ?, ?, ?, ?) RETURNING id",
      {run.id, db::OptionalText(options_.tag), options_.user_name,
       util::FormatDatabaseTime(run_history_time), db::OptionalText(cc_version),
       db::OptionalText(options_.description)});
  if (!history.ok()) return TaskResult<StoredRun>(history.error());
  kLog.Debug("Adding run done.");
  const int64_t history_id = history.value().value_or(0);
  if (TaskResult<void> stored = StoreAnalysisStatistics(history_id);
      !stored.ok()) {
    return TaskResult<StoredRun>(stored.error());
  }
  if (TaskResult<void> stored = StoreAnalysisInfo(history_id); !stored.ok()) {
    return TaskResult<StoredRun>(stored.error());
  }
  kLog.Debug("Storing analysis statistics done.");
  return TaskResult<StoredRun>(run);
}

TaskResult<void> MassStoreRun::StoreAnalysisStatistics(int64_t history_id) {
  // The statistics of an analyzer over all metadata files.
  struct Statistics {
    std::set<std::string> versions;
    std::set<std::string> failed_sources;
    std::set<std::string> successful_sources;
    int64_t successful = 0;
  };
  cc::api::OrderedMap<std::string, Statistics> statistics;
  for (const auto& [folder, metadata] : metadata_) {
    double durations = 0.0;
    for (const double duration : metadata.check_durations) {
      durations += duration;
    }
    duration_ += static_cast<int64_t>(durations);
    for (const auto& [analyzer, result] : metadata.analyzer_statistics) {
      Statistics& stats = statistics[analyzer];
      if (result.version) stats.versions.insert(*result.version);
      if (result.failed_sources) {
        if (options_.client_version == "6.9.0") {
          stats.failed_sources.insert("Unavailable in CodeChecker 6.9.0!");
        } else {
          stats.failed_sources.insert(result.failed_sources->begin(),
                                      result.failed_sources->end());
        }
      }
      if (result.successful_sources) {
        stats.successful_sources.insert(result.successful_sources->begin(),
                                        result.successful_sources->end());
      }
      stats.successful += result.successful.value_or(0);
    }
  }
  // Returns `texts` joined by `separator` and compressed; NULL if empty.
  auto compressed = [](const std::set<std::string>& texts,
                       std::string_view separator) {
    if (texts.empty()) return db::Param(nullptr);
    return db::Param(db::Bytes{util::ZlibCompress(util::Join(
        std::vector<std::string>(texts.begin(), texts.end()), separator))});
  };
  for (const auto& [analyzer, stats] : statistics) {
    const int64_t successful =
        stats.successful_sources.empty()
            ? stats.successful
            : static_cast<int64_t>(stats.successful_sources.size());
    TaskResult<db::QueryResult> added = Query(
        "INSERT INTO analyzer_statistics (run_history_id, analyzer_type, "
        "version, successful, failed, failed_files) VALUES (?, ?, ?, ?, ?, ?)",
        {history_id, analyzer, compressed(stats.versions, "; "), successful,
         static_cast<int64_t>(stats.failed_sources.size()),
         compressed(stats.failed_sources, "\n")});
    if (!added.ok()) return TaskResult<void>(added.error());
  }
  return TaskResult<void>();
}

TaskResult<int64_t> MassStoreRun::CheckerSetId(std::vector<int64_t> enabled,
                                               std::vector<int64_t> disabled) {
  // Python: CheckerSet.compute_hash(), the SHA-256 of json.dumps({"d": ...,
  // "e": ...}, sort_keys=True, separators=(',', ':')).
  std::ranges::sort(enabled);
  std::ranges::sort(disabled);
  auto list = [](const std::vector<int64_t>& ids) {
    std::vector<std::string> texts;
    for (const int64_t id : ids) texts.push_back(std::to_string(id));
    return util::Join(texts, ",");
  };
  const std::string hash = *util::HexDigest(
      "sha256",
      std::format("{{\"d\":[{}],\"e\":[{}]}}", list(disabled), list(enabled)));
  constexpr std::string_view kSelect =
      "SELECT id FROM checker_set WHERE hash_digest = ?";
  TaskResult<std::optional<int64_t>> id = QueryId(kSelect, {hash});
  if (!id.ok()) return TaskResult<int64_t>(id.error());
  if (!id.value()) {
    // Another store may create the same set at the same time.
    id = QueryId(
        "INSERT INTO checker_set (hash_digest) VALUES (?) ON CONFLICT DO "
        "NOTHING RETURNING id",
        {hash});
    if (!id.ok()) return TaskResult<int64_t>(id.error());
    if (id.value()) {
      kLog.Info(std::format("[{}] Created new CheckerSet with hash '{}'",
                            options_.run_name, hash));
      const int64_t set_id = *id.value();
      // Adds the checkers `ids` to the set, `is_enabled` or not.
      auto add_items = [this, set_id](const std::vector<int64_t>& ids,
                                      bool is_enabled) {
        for (const int64_t checker : ids) {
          TaskResult<db::QueryResult> added = Query(
              "INSERT INTO checker_set_items (checker_set_id, checker_id, "
              "enabled) VALUES (?, ?, ?)",
              {set_id, checker, is_enabled});
          if (!added.ok()) return TaskResult<void>(added.error());
        }
        return TaskResult<void>();
      };
      if (TaskResult<void> added = add_items(enabled, true); !added.ok()) {
        return TaskResult<int64_t>(added.error());
      }
      if (TaskResult<void> added = add_items(disabled, false); !added.ok()) {
        return TaskResult<int64_t>(added.error());
      }
    } else {
      id = QueryId(kSelect, {hash});
      if (!id.ok()) return TaskResult<int64_t>(id.error());
    }
  }
  if (!id.value()) {
    return TaskResult<int64_t>(
        TaskError{.message = "Failed to query CheckerSet from database!"});
  }
  return TaskResult<int64_t>(*id.value());
}

TaskResult<void> MassStoreRun::StoreAnalysisInfo(int64_t history_id) {
  for (const auto& [folder, metadata] : metadata_) {
    for (const std::string& command : metadata.check_commands) {
      std::vector<int64_t> enabled;
      std::vector<int64_t> disabled;
      for (const auto& [analyzer, checkers] : metadata.checkers) {
        TaskResult<db::QueryResult> rows = Query(
            "SELECT id, checker_name FROM checkers WHERE analyzer_name = ?",
            {analyzer});
        if (!rows.ok()) return TaskResult<void>(rows.error());
        std::map<std::string, int64_t> ids;
        for (const db::Row& row : rows.value().rows) {
          ids[row.Text(1)] = row.Int(0);
        }
        for (const auto& [checker, is_enabled] : checkers) {
          const auto id = ids.find(checker);
          if (id == ids.end()) {
            return TaskResult<void>(TaskFailure(util::KeyError(checker)));
          }
          (is_enabled ? enabled : disabled).push_back(id->second);
        }
      }
      const TaskResult<int64_t> checker_set =
          CheckerSetId(std::move(enabled), std::move(disabled));
      if (!checker_set.ok()) return TaskResult<void>(checker_set.error());
      TaskResult<std::optional<int64_t>> info = QueryId(
          "INSERT INTO analysis_info (analyzer_command, checker_set_id) "
          "VALUES (?, ?) RETURNING id",
          {db::Bytes{util::ZlibCompress(command)}, checker_set.value()});
      if (!info.ok()) return TaskResult<void>(info.error());
      const int64_t info_id = info.value().value_or(0);
      TaskResult<db::QueryResult> linked = Query(
          "INSERT INTO run_history_analysis_info (run_history_id, "
          "analysis_info_id) VALUES (?, ?)",
          {history_id, info_id});
      if (!linked.ok()) return TaskResult<void>(linked.error());
      analysis_info_[folder] = info_id;
      if (TaskResult<void> stored = StoreAnalysisInfoFiles(info_id, folder);
          !stored.ok()) {
        return stored;
      }
    }
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::StoreAnalysisInfoFiles(
    int64_t analysis_info_id, const std::string& folder) {
  const std::string conf_dir = util::pypath::Join({folder, "conf"});
  if (!util::pypath::IsDir(conf_dir)) return TaskResult<void>();
  for (const std::string& name : platform::ListDirectory(conf_dir)) {
    const TaskResult<std::string> hash = AddFileContent(
        util::pypath::Join({conf_dir, name}), std::nullopt);
    if (!hash.ok()) return TaskResult<void>(hash.error());
    const std::vector<db::Param> key = {analysis_info_id, name, hash.value()};
    TaskResult<db::QueryResult> stored = Query(
        "SELECT analysis_info_id FROM analysis_info_files WHERE "
        "analysis_info_id = ? AND filename = ? AND content_hash = ?",
        key);
    if (!stored.ok()) return TaskResult<void>(stored.error());
    if (!stored.value().rows.empty()) continue;
    TaskResult<db::QueryResult> added = Query(
        "INSERT INTO analysis_info_files (analysis_info_id, filename, "
        "content_hash) VALUES (?, ?, ?)",
        key);
    if (!added.ok()) return TaskResult<void>(added.error());
  }
  return TaskResult<void>();
}

TaskResult<cc::api::OrderedMap<std::string, std::vector<OldReport>>>
MassStoreRun::LoadOldReports(int64_t run_id) {
  using OldReports = cc::api::OrderedMap<std::string, std::vector<OldReport>>;
  TaskResult<db::QueryResult> rows = Query(
      "SELECT reports.id, reports.bug_id, reports.detection_status, "
      "reports.detected_at, reports.review_status, reports.fixed_at, "
      "checkers.checker_name FROM reports LEFT OUTER JOIN checkers ON "
      "checkers.id = reports.checker_id WHERE reports.run_id = ? "
      "ORDER BY reports.id",
      {run_id});
  if (!rows.ok()) return TaskResult<OldReports>(rows.error());
  OldReports reports;
  for (const db::Row& row : rows.value().rows) {
    reports[row.Text(1)].push_back(
        {.id = row.Int(0),
         .detection_status = row.Text(2),
         .detected_at = util::ParseDatabaseTime(row.Text(3)).value_or(0),
         .review_status = row.Text(4),
         .fixed_at = row.IsNull(5) ? std::nullopt
                                   : util::ParseDatabaseTime(row.Text(5)),
         .checker_name = row.Text(6)});
  }
  return TaskResult<OldReports>(std::move(reports));
}

// Returns the skip list of the report folder `folder`: its skip_file, if
// it has one (Python: get_skip_handler() of __store_reports()).
TaskResult<common::SkipListHandler> SkipHandlerOf(const std::string& folder) {
  using Handler = TaskResult<common::SkipListHandler>;
  const std::string skip_file = util::pypath::Join({folder, "skip_file"});
  if (!util::pypath::Exists(skip_file)) {
    return Handler(common::SkipListHandler(""));
  }
  kLog.Debug(std::format("Pocessing skip file {}", skip_file));
  const Result<std::string> content = util::ReadTextFile(skip_file);
  if (!content.ok()) {
    kLog.Error(std::format("Failed to open skip file: {}",
                           content.error().message));
    return Handler(TaskFailure(content.error()));
  }
  kLog.Debug(content.value());
  return Handler(common::SkipListHandler(content.value()));
}

TaskResult<void> MassStoreRun::StoreReports(
    const std::string& report_dir, const std::string& source_root,
    int64_t run_id, const std::map<std::string, int64_t>& file_ids,
    int64_t run_history_time) {
  added_path_hashes_.clear();
  new_report_hashes_.clear();
  report_checkers_.clear();
  auto old_reports = LoadOldReports(run_id);
  if (!old_reports.ok()) return TaskResult<void>(old_reports.error());
  std::set<std::string> enabled_checkers;
  std::set<std::string> disabled_checkers;
  int processed_result_files = 0;
  for (const platform::WalkedFolder& folder : platform::Walk(report_dir)) {
    kLog.Debug(std::format("Get reports from '{}' directory", folder.path));
    TaskResult<common::SkipListHandler> skip_handler =
        SkipHandlerOf(folder.path);
    if (!skip_handler.ok()) return TaskResult<void>(skip_handler.error());
    common::ReviewStatusHandler review_status_handler(source_root);
    const std::string review_status_file =
        util::pypath::Join({folder.path, "review_status.yaml"});
    if (util::pypath::IsFile(review_status_file)) {
      if (Result<void> read =
              review_status_handler.SetReviewStatusConfig(review_status_file);
          !read.ok()) {
        return TaskResult<void>(TaskFailure(read.error()));
      }
    }
    const auto metadata = metadata_.find(folder.path);
    if (metadata == metadata_.end()) {
      return TaskResult<void>(TaskFailure(util::KeyError(folder.path)));
    }
    enabled_checkers.insert(metadata->second.enabled_checkers.begin(),
                            metadata->second.enabled_checkers.end());
    disabled_checkers.insert(metadata->second.disabled_checkers.begin(),
                             metadata->second.disabled_checkers.end());
    for (const std::string& file : folder.files) {
      if (!report::IsSupported(file)) continue;
      kLog.Debug(std::format("Parsing input file '{}'",
                             util::pypath::Basename(file)));
      if (TaskResult<void> go_on = CancelIfRequested(); !go_on.ok()) {
        return go_on;
      }
      if (TaskResult<void> processed = ProcessReportFile(
              file, run_id, file_ids, run_history_time, skip_handler.value(),
              review_status_handler, old_reports.value());
          !processed.ok()) {
        return processed;
      }
      ++processed_result_files;
    }
  }
  if (TaskResult<void> stored = AddReportContext(file_ids); !stored.ok()) {
    return stored;
  }
  if (TaskResult<void> applied = ApplyReviewStatusRules(
          run_id, old_reports.value(), run_history_time);
      !applied.ok()) {
    return applied;
  }
  kLog.Info(std::format("[{}] Processed {} analyzer result file(s).",
                        options_.run_name, processed_result_files));
  // A checker that reported can not be disabled, also if another report
  // folder disabled it.
  enabled_checkers.insert(report_checkers_.begin(), report_checkers_.end());
  for (const std::string& checker : report_checkers_) {
    disabled_checkers.erase(checker);
  }
  return UpdateOldReports(old_reports.value(), enabled_checkers,
                          disabled_checkers, run_history_time);
}

TaskResult<void> MassStoreRun::ProcessReportFile(
    const std::string& path, int64_t run_id,
    const std::map<std::string, int64_t>& file_ids, int64_t run_history_time,
    const common::SkipListHandler& skip_handler,
    common::ReviewStatusHandler& review_status_handler,
    const cc::api::OrderedMap<std::string, std::vector<OldReport>>&
        old_reports) {
  std::vector<report::Report> reports = report::GetReports(path);
  const std::string folder = util::pypath::Dirname(path);
  const auto metadata = metadata_.find(folder);
  if (reports.empty()) return TaskResult<void>();
  if (metadata == metadata_.end()) {
    return TaskResult<void>(TaskFailure(util::KeyError(folder)));
  }
  const auto info = analysis_info_.find(folder);
  const std::optional<int64_t> analysis_info_id =
      info == analysis_info_.end() ? std::nullopt
                                   : std::optional<int64_t>(info->second);
  report::TrimmedPaths trimmed_paths;
  // The reports of a result file share a few source files.
  report::SourceFileCache sources;
  for (report::Report& report : reports) {
    ++report_count_;
    report::TrimPaths(trim_patterns_, &report, &trimmed_paths);
    report::ApplyTrimmedPaths(trimmed_paths, &report);
    std::set<std::string> missing;
    for (const report::File* file : report::FilesOf(&report)) {
      if (!file_ids.contains(file->path)) missing.insert(file->path);
    }
    if (!missing.empty()) {
      kLog.Warning(std::format(
          "Failed to get database id for file path '{}'! Skip adding "
          "report: {}:{}:{} [{}]",
          util::Join(std::vector<std::string>(missing.begin(), missing.end()),
                     " "),
          report.file.path, report.line, report.column, report.checker_name));
      continue;
    }
    report_checkers_.insert(report.checker_name);
    if (skip_handler.ShouldSkip(report.file.original_path)) continue;
    const std::string path_hash = report::GetReportPathHash(report);
    if (added_path_hashes_.contains(path_hash)) {
      kLog.Debug("Not storing report. Already added: " +
                 report::ReportRepr(report));
      continue;
    }
    kLog.Debug("Storing report to the database...");
    std::string_view detection_status = "new";
    int64_t detected_at = run_history_time;
    const std::string report_hash = report.report_hash.value_or("");
    const OldReport* old_report = nullptr;
    if (const auto old = old_reports.find(report_hash);
        old != old_reports.end()) {
      old_report = &old->second.front();
      detection_status = old_report->detection_status == "resolved"
                             ? "reopened"
                             : "unresolved";
      detected_at = old_report->detected_at;
    }
    if (const auto analyzer =
            metadata->second.checker_to_analyzer.find(report.checker_name);
        analyzer != metadata->second.checker_to_analyzer.end()) {
      report.analyzer_name = analyzer->second;
    }
    Result<report::ReviewStatus> review_status =
        review_status_handler.GetReviewStatus(report, sources);
    if (!review_status.ok()) {
      wrong_source_code_comments_.push_back(review_status.error().message);
      review_status = Result<report::ReviewStatus>(report::ReviewStatus());
    }
    // False positive and intentional reports count as closed, so they get
    // a fix date.
    std::optional<int64_t> fixed_at;
    const std::string& status = review_status.value().status;
    if (status == "false_positive" || status == "intentional") {
      const bool was_closed =
          old_report && (old_report->review_status == "false_positive" ||
                         old_report->review_status == "intentional");
      fixed_at = was_closed ? old_report->fixed_at
                            : std::optional<int64_t>(run_history_time);
    }
    if (static_cast<int64_t>(added_reports_.size()) >= report_limit_) {
      kLog.Error(std::format(
          "The number of reports in the given report folder is larger than "
          "the allowed.The limit: {}!",
          report_limit_));
      return TaskResult<void>(TaskError{.message = std::format(
          "**Report Limit Exceeded** This report folder cannot be stored "
          "because the number of reports in the result folder is too high. "
          "Usually noisy checkers, generating a lot of reports are not "
          "useful and it is better to disable them. Run `CodeChecker parse "
          "<report_folder>` to gain a better understanding on report counts. "
          "Disable checkers that have generated an excessive number of "
          "reports and then rerun the analysis to be able to store the "
          "results on the server. Limit: {}",
          report_limit_)});
    }
    if (TaskResult<void> added = AddReport(
            report, path_hash, run_id, file_ids.at(report.file.path),
            review_status.value(), detection_status, detected_at,
            run_history_time, analysis_info_id, fixed_at);
        !added.ok()) {
      return added;
    }
    new_report_hashes_[report_hash] = status;
    added_path_hashes_.insert(path_hash);
    kLog.Debug(std::format(
        "Storing report done. path_hash={}, bug_id/report_hash={}, "
        "source_file={}",
        path_hash, report_hash, path));
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::AddReport(
    const report::Report& report, const std::string& path_hash,
    int64_t run_id, int64_t file_id,
    const report::ReviewStatus& review_status,
    std::string_view detection_status, int64_t detected_at,
    int64_t run_history_time, std::optional<int64_t> analysis_info_id,
    std::optional<int64_t> fixed_at) {
  const CheckerName checker = CheckerOf(report);
  TaskResult<std::optional<int64_t>> checker_id = CheckerId(checker);
  if (!checker_id.ok()) return TaskResult<void>(checker_id.error());
  // A new checker gets its row after the reports are committed, so a
  // failure of a concurrent store to insert the same row cannot lose them.
  const bool fake = !checker_id.value();
  if (fake) {
    checker_id = CheckerId({std::string(kFakeName), std::string(kFakeName)});
    if (!checker_id.ok()) return TaskResult<void>(checker_id.error());
    if (!checker_id.value()) {
      return TaskResult<void>(TaskFailure(util::KeyError(kFakeName)));
    }
  }
  TaskResult<std::optional<int64_t>> id = QueryId(
      "INSERT INTO reports (file_id, run_id, bug_id, checker_id, line, "
      "\"column\", path_length, checker_message, detection_status, "
      "review_status, review_status_author, review_status_message, "
      "review_status_date, review_status_is_in_source, detected_at, "
      "fixed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "RETURNING id",
      {file_id, run_id, db::OptionalText(report.report_hash),
       *checker_id.value(), int64_t{report.line}, int64_t{report.column},
       static_cast<int64_t>(report.bug_path_events.size()), report.message,
       std::string(detection_status), review_status.status,
       options_.user_name, db::Bytes{review_status.message},
       util::FormatDatabaseTime(run_history_time),
       !review_status.source_comment.empty(),
       util::FormatDatabaseTime(detected_at), db::OptionalTime(fixed_at)});
  if (!id.ok()) return TaskResult<void>(id.error());
  const int64_t report_id = id.value().value_or(0);
  if (analysis_info_id) {
    TaskResult<db::QueryResult> linked = Query(
        "INSERT INTO report_analysis_info (report_id, analysis_info_id) "
        "VALUES (?, ?)",
        {report_id, *analysis_info_id});
    if (!linked.ok()) return TaskResult<void>(linked.error());
  }
  added_reports_.push_back({.id = report_id, .report = report});
  if (fake) {
    fake_checker_reports_[path_hash] = {.checker = checker, .id = report_id};
  }
  return TaskResult<void>();
}

// An item of the bug path data of a report (Python: ReportPathData.Item).
struct PathItem {
  const report::File* file;
  const std::optional<report::Range>* range;
  std::string_view type;
  const std::string* message;  // Null: none.
};

TaskResult<void> MassStoreRun::AddReportContext(
    const std::map<std::string, int64_t>& file_ids) {
  std::set<std::pair<int64_t, int64_t>> path_data_files;
  for (const AddedReport& added : added_reports_) {
    const report::Report& report = added.report;
    std::vector<PathItem> items;
    for (const report::BugPathPosition& position :
         report.bug_path_positions) {
      items.push_back({&position.file, &position.range, "path", nullptr});
    }
    for (const report::BugPathEvent& event : report.bug_path_events) {
      items.push_back({&event.file, &event.range, "event", &event.message});
    }
    for (const report::BugPathEvent& note : report.notes) {
      items.push_back({&note.file, &note.range, "note", &note.message});
    }
    for (const report::MacroExpansion& macro : report.macro_expansions) {
      items.push_back({&macro.event.file, &macro.event.range, "macro",
                       &macro.event.message});
    }
    const util::MutableJsonDocument document = util::NewJsonDocument();
    yyjson_mut_doc* doc = document.get();
    yyjson_mut_val* path_data = yyjson_mut_arr(doc);
    for (const PathItem& item : items) {
      if (!*item.range) {
        return TaskResult<void>(TaskError{
            .message = "'NoneType' object has no attribute 'start_line'"});
      }
      const report::Range& range = **item.range;
      const int64_t file_id = file_ids.at(item.file->path);
      yyjson_mut_val* object = yyjson_mut_obj(doc);
      yyjson_mut_obj_add_int(doc, object, "from_row", range.start_line);
      yyjson_mut_obj_add_int(doc, object, "from_col", range.start_col);
      yyjson_mut_obj_add_int(doc, object, "to_row", range.end_line);
      yyjson_mut_obj_add_int(doc, object, "to_col", range.end_col);
      yyjson_mut_obj_add_int(doc, object, "file_id", file_id);
      yyjson_mut_obj_add_val(doc, object, "type",
                             util::MakeJsonString(doc, item.type));
      yyjson_mut_obj_add_val(
          doc, object, "msg",
          item.message ? util::MakeJsonString(doc, *item.message)
                       : yyjson_mut_null(doc));
      yyjson_mut_arr_append(path_data, object);
      path_data_files.insert({added.id, file_id});
    }
    TaskResult<db::QueryResult> stored = Query(
        "INSERT INTO report_path_data (report_id, path_data) VALUES (?, ?)",
        {added.id, db::Bytes{util::ZlibCompress(util::DumpJson(path_data))}});
    if (!stored.ok()) return TaskResult<void>(stored.error());
    if (TaskResult<void> annotated =
            AddReportAnnotations(added.id, report.annotations);
        !annotated.ok()) {
      return annotated;
    }
  }
  for (const auto& [report_id, file_id] : path_data_files) {
    TaskResult<db::QueryResult> stored = Query(
        "INSERT INTO report_path_data_files (report_path_data_id, file_id) "
        "VALUES (?, ?)",
        {report_id, file_id});
    if (!stored.ok()) return TaskResult<void>(stored.error());
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::AddReportAnnotations(
    int64_t report_id,
    const std::vector<std::pair<std::string, std::string>>& annotations) {
  for (const auto& [key, value] : annotations) {
    // Python: report_annotation_types; the value is stored as str() writes
    // the converted value.
    std::optional<std::string> stored;
    if (key == "timestamp") {
      stored = NormalizeIsoTime(value);
    } else if (key == "testcase") {
      stored = value;
    } else if (key == "chronological_order") {
      if (const std::optional<int64_t> order = util::ParseInt(value)) {
        stored = std::to_string(*order);
      }
    }
    if (!stored) {
      return TaskResult<void>(
          TaskError{.message = std::string(kAnnotationError)});
    }
    TaskResult<db::QueryResult> added = Query(
        "INSERT INTO report_annotations (report_id, \"key\", value) VALUES "
        "(?, ?, ?)",
        {report_id, key, *stored});
    if (!added.ok()) return TaskResult<void>(added.error());
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::ApplyReviewStatusRules(
    int64_t run_id,
    const cc::api::OrderedMap<std::string, std::vector<OldReport>>&
        old_reports,
    int64_t run_history_time) {
  TaskResult<db::QueryResult> rules = Query(
      "SELECT review_statuses.status, review_statuses.message, reports.id, "
      "reports.bug_id FROM review_statuses JOIN reports ON reports.bug_id = "
      "review_statuses.bug_hash WHERE reports.run_id = ? AND "
      "reports.review_status_is_in_source IS false",
      {run_id});
  if (!rules.ok()) return TaskResult<void>(rules.error());
  for (const db::Row& row : rules.value().rows) {
    const std::string bug_id = row.Text(3);
    if (!new_report_hashes_.contains(bug_id)) continue;
    const std::string status = row.Text(0);
    std::optional<int64_t> fixed_at;
    if (status == "false_positive" || status == "intentional") {
      fixed_at = run_history_time;
      if (const auto old = old_reports.find(bug_id);
          old != old_reports.end() &&
          (old->second.front().review_status == "false_positive" ||
           old->second.front().review_status == "intentional")) {
        fixed_at = old->second.front().fixed_at;
      }
    }
    // The reference means to copy the author and the date of the rule too,
    // but sets misspelled attributes, so the report keeps its own.
    TaskResult<db::QueryResult> updated = Query(
        "UPDATE reports SET review_status = ?, review_status_message = ?, "
        "fixed_at = ?, review_status_is_in_source = false WHERE id = ?",
        {status, db::Bytes{row.Blob(1)}, db::OptionalTime(fixed_at),
         row.Int(2)});
    if (!updated.ok()) return TaskResult<void>(updated.error());
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::UpdateOldReports(
    const cc::api::OrderedMap<std::string, std::vector<OldReport>>&
        old_reports,
    const std::set<std::string>& enabled_checkers,
    const std::set<std::string>& disabled_checkers,
    int64_t run_history_time) {
  std::vector<int64_t> to_delete;
  std::map<std::string, std::vector<int64_t>> by_status;
  for (const auto& [bug_hash, reports] : old_reports) {
    for (const OldReport& report : reports) {
      if (new_report_hashes_.contains(bug_hash)) {
        to_delete.push_back(report.id);
      } else if (disabled_checkers.contains(report.checker_name)) {
        by_status["off"].push_back(report.id);
      } else if (CheckerIsUnavailable(report.checker_name,
                                      enabled_checkers)) {
        by_status["unavailable"].push_back(report.id);
      } else {
        by_status["resolved"].push_back(report.id);
      }
    }
  }
  const std::string time = util::FormatDatabaseTime(run_history_time);
  for (const auto& [status, ids] : by_status) {
    for (const std::vector<int64_t>& chunk : db::Chunks(ids)) {
      db::Sql update("UPDATE reports SET detection_status = ");
      update.Bind(status)
          .Add(", fixed_at = COALESCE(fixed_at, ")
          .Bind(time)
          .Add(") WHERE ")
          .Add(db::In("id", chunk));
      TaskResult<db::QueryResult> updated =
          Query(update.text(), update.params());
      if (!updated.ok()) return TaskResult<void>(updated.error());
    }
  }
  // Python: remove_reports().
  for (const std::vector<int64_t>& chunk : db::Chunks(to_delete)) {
    db::Sql remove("DELETE FROM reports WHERE ");
    remove.Add(db::In("id", chunk));
    TaskResult<db::QueryResult> removed = Query(remove.text(), remove.params());
    if (!removed.ok()) return TaskResult<void>(removed.error());
  }
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::RealiseFakeCheckers() {
  std::map<CheckerName, std::vector<int64_t>> by_checker;
  for (const auto& [path_hash, report] : fake_checker_reports_) {
    by_checker[report.checker].push_back(report.id);
  }
  for (const auto& [checker, report_ids] : by_checker) {
    TaskResult<std::optional<int64_t>> checker_id = CheckerId(checker);
    if (!checker_id.ok()) return TaskResult<void>(checker_id.error());
    if (!checker_id.value()) {
      return TaskResult<void>(TaskError{
          .message = "'NoneType' object has no attribute 'id'"});
    }
    for (const std::vector<int64_t>& chunk : db::Chunks(report_ids)) {
      db::Sql update("UPDATE reports SET checker_id = ");
      update.Bind(*checker_id.value()).Add(" WHERE ").Add(db::In("id", chunk));
      TaskResult<db::QueryResult> updated =
          Query(update.text(), update.params());
      if (!updated.ok()) return TaskResult<void>(updated.error());
    }
  }
  return TaskResult<void>();
}

void MassStoreRun::FinishCheckerRun(int64_t run_id) {
  kLog.Debug("Finishing checker run");
  const Result<db::QueryResult> finished = db_->Query(
      "UPDATE runs SET duration = ? WHERE id = ?", {duration_, run_id});
  if (!finished.ok()) kLog.Error(finished.error().message);
}

TaskResult<void> MassStoreRun::DropRunLock() {
  kLog.Debug(std::format("Releasing 'run_lock' from run '{}' ...",
                         options_.run_name));
  std::string locked_at;
  std::string username;
  const TaskResult<void> dropped = InTransaction([&]() {
    TaskResult<db::QueryResult> lock =
        Query(RunLockQuery(*db_), {options_.run_name});
    if (!lock.ok()) return TaskResult<void>(lock.error());
    if (lock.value().rows.empty()) {
      return TaskResult<void>(
          TaskError{.message = "No row was found when one was required"});
    }
    locked_at = util::DatabaseTimeToPython(lock.value().rows.front().Text(0));
    username = lock.value().rows.front().Text(1);
    TaskResult<db::QueryResult> removed = Query(
        "DELETE FROM run_locks WHERE name = ?", {options_.run_name});
    return removed.ok() ? TaskResult<void>()
                        : TaskResult<void>(removed.error());
  });
  if (dropped.ok()) {
    kLog.Debug(std::format(
        "Released 'run_lock' (originally acquired by '{}' on '{}') from run "
        "'{}'.",
        username, locked_at, options_.run_name));
  }
  return dropped;
}

// Returns the paths and content hashes of content_hashes.json at `path`, in
// the order of the file (Python: load_json(path, {})).
cc::api::OrderedMap<std::string, std::string> LoadContentHashes(
    const std::string& path) {
  cc::api::OrderedMap<std::string, std::string> hashes;
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  if (!document) return hashes;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  yyjson_obj_foreach(yyjson_doc_get_root(document->get()), index, count, key,
                     value) {
    hashes[util::JsonString(key)] = util::JsonString(value);
  }
  return hashes;
}

TaskResult<void> MassStoreRun::StoreLocked(
    int64_t zip_size, double preparation_seconds,
    std::chrono::steady_clock::time_point start) {
  const std::string& run_name = options_.run_name;
  kLog.Debug(std::format("Using unzipped folder '{}'", zip_dir_));
  const std::string source_root = util::pypath::Join({zip_dir_, "root"});
  const std::string report_dir = util::pypath::Join({zip_dir_, "reports"});
  const cc::api::OrderedMap<std::string, std::string> hashes =
      LoadContentHashes(util::pypath::Join({zip_dir_, "content_hashes.json"}));
  TaskResult<std::map<std::string, int64_t>> file_ids(
      std::map<std::string, int64_t>{});
  {
    StepLog step(run_name, "Store source files");
    kLog.Info(std::format("[{}] Storing {} source file(s).", run_name,
                          hashes.size()));
    file_ids = StoreSourceFiles(source_root, hashes);
    if (!file_ids.ok()) return TaskResult<void>(file_ids.error());
    if (TaskResult<void> blamed = AddBlameInfo(
            util::pypath::Join({zip_dir_, "blame"}), hashes);
        !blamed.ok()) {
      return blamed;
    }
  }
  const int64_t run_history_time = util::LocalNow();
  {
    StepLog step(run_name, "Parse 'metadata.json's");
    for (const platform::WalkedFolder& folder : platform::Walk(report_dir)) {
      if (TaskResult<void> go_on = CancelIfRequested(); !go_on.ok()) {
        return go_on;
      }
      metadata_[folder.path] =
          ParseMetadata(util::pypath::Join({folder.path, "metadata.json"}));
    }
  }
  if (TaskResult<void> go_on = CancelIfRequested(); !go_on.ok()) return go_on;
  {
    StepLog step(run_name, "Store look-up ID for checkers in 'metadata.json'");
    std::set<CheckerName> checkers;
    for (const auto& [folder, metadata] : metadata_) {
      for (const auto& [analyzer, names] : metadata.checkers) {
        for (const auto& [checker, enabled] : names) {
          checkers.insert({analyzer, checker});
        }
      }
    }
    if (TaskResult<void> stored = StoreCheckerIdentifiers(std::move(checkers));
        !stored.ok()) {
      return stored;
    }
  }
  StoredRun run;
  // This transaction holds the whole store.
  if (TaskResult<void> stored = InTransaction([&]() {
        if (TaskResult<void> locked = SelectRunLock(); !locked.ok()) {
          return locked;
        }
        TaskResult<StoredRun> added = AddOrUpdateRun(run_history_time);
        if (!added.ok()) return TaskResult<void>(added.error());
        run = added.value();
        {
          StepLog step(run_name, "Store 'reports'");
          if (TaskResult<void> reports =
                  StoreReports(report_dir, source_root, run.id,
                               file_ids.value(), run_history_time);
              !reports.ok()) {
            return reports;
          }
        }
        return CancelIfRequested();
      });
      !stored.ok()) {
    return stored;
  }
  // The task is not cancelled from here on: the reports are committed, and
  // the users would see their fake checkers.
  if (!fake_checker_reports_.empty()) {
    StepLog step(run_name,
                 "Get look-up IDs for checkers not present in "
                 "'metadata.json'");
    std::set<CheckerName> checkers;
    for (const auto& [path_hash, report] : fake_checker_reports_) {
      checkers.insert(report.checker);
    }
    if (TaskResult<void> stored = StoreCheckerIdentifiers(std::move(checkers));
        !stored.ok()) {
      return stored;
    }
  }
  if (TaskResult<void> finished = InTransaction([&]() {
        if (TaskResult<void> locked = SelectRunLock(); !locked.ok()) {
          return locked;
        }
        if (!fake_checker_reports_.empty()) {
          StepLog step(run_name, "Fix-up report-to-checker associations");
          if (TaskResult<void> fixed = RealiseFakeCheckers(); !fixed.ok()) {
            return fixed;
          }
        }
        FinishCheckerRun(run.id);
        return TaskResult<void>();
      });
      !finished.ok()) {
    return finished;
  }
  const double run_time = SecondsSince(start) + preparation_seconds;
  // A store into an existing run does not add a run.
  if (Result<void> cached = SetCachedRunData(
          context_->config_db(), product_->id(), product_->endpoint(),
          run.updated ? 0 : 1, run_history_time);
      !cached.ok()) {
    return TaskResult<void>(TaskFailure(cached.error()));
  }
  kLog.Info(std::format(
      "User '{}' stored results to product '{}', run '{}' in {:.2f} seconds "
      "(run id: {}{}, report count: {}, decompressed size: {:.2f} KiB).",
      options_.user_name, product_->display_name(), run_name, run_time, run.id,
      options_.tag && !options_.tag->empty()
          ? std::format(", under tag: '{}'", *options_.tag)
          : "",
      report_count_, static_cast<double>(zip_size) / 1024.0));
  return TaskResult<void>();
}

TaskResult<void> MassStoreRun::Store(int64_t zip_size,
                                     double preparation_seconds) {
  if (TaskResult<void> go_on = CancelIfRequested(); !go_on.ok()) return go_on;
  const auto start = std::chrono::steady_clock::now();
  const TaskResult<void> stored =
      StoreLocked(zip_size, preparation_seconds, start);
  if (!stored.ok() && !stored.error().cancelled) {
    kLog.Error(
        std::format("Failed to store results: {}", stored.error().message));
  }
  // The lock is removed however the store ended, and the invalid comments
  // fail the task even after the data was stored, as in the reference.
  if (TaskResult<void> dropped = DropRunLock(); !dropped.ok()) return dropped;
  if (!wrong_source_code_comments_.empty()) {
    return TaskResult<void>(WrongCommentsError(wrong_source_code_comments_));
  }
  return stored;
}

}  // namespace

ApiResult<std::string> SubmitStore(const Request& request,
                                   const StoreOptions& options,
                                   const std::string& zip_base64,
                                   bool asynchronous) {
  const auto start = std::chrono::steady_clock::now();
  if (options.run_name.empty()) {
    return ApiResult<std::string>(
        ApiError{.code = ErrorCode::GENERAL,
                 .message = "A run name is needed to know where to store!"});
  }
  if (request.server->task_worker_count() == 0) {
    return ApiResult<std::string>(
        ApiError{.code = ErrorCode::GENERAL,
                 .message = "No task worker process is available!"});
  }
  const Result<db::QueryResult> product = request.server->config_db().Query(
      "SELECT run_limit FROM products WHERE id = ?", {request.product->id()});
  if (!product.ok()) {
    return ApiResult<std::string>(DatabaseError(product.error()));
  }
  if (product.value().rows.empty()) {
    const Error error = util::KeyError(
        std::format("No product with ID '{}'", request.product->id()));
    return ApiResult<std::string>(ApiError{.message = error.message});
  }
  if (ApiResult<void> checked = CheckRunLimit(
          request, product.value().rows.front().OptionalInt(0),
          options.run_name);
      !checked.ok()) {
    return ApiResult<std::string>(checked.error());
  }
  Result<db::Connection*> db = request.product->Connection();
  if (!db.ok()) return ApiResult<std::string>(DatabaseError(db.error()));
  if (ApiResult<void> locked =
          StoreRunLock(*db.value(), options.run_name, options.user_name);
      !locked.ok()) {
    return ApiResult<std::string>(locked.error());
  }
  ApiResult<tasks::QueuedTask> task = CreateMassStoreTask(
      request, options, zip_base64, asynchronous, start);
  if (!task.ok()) return ApiResult<std::string>(task.error());
  // The legacy massStoreRun() waits until the worker closes its copy of the
  // write end of this pipe.
  std::error_code error;
  std::optional<platform::Pipe> done;
  if (!asynchronous) {
    done = platform::MakePipe(&error);
    if (!done) {
      return ApiResult<std::string>(ApiError{.message = error.message()});
    }
  }
  if (Result<void> pushed = request.server->tasks().PushTask(
          task.value(), done ? &done->write_end : nullptr);
      !pushed.ok()) {
    return ApiResult<std::string>(ApiError{.message = pushed.error().message});
  }
  if (done) {
    kLog.Info(std::format(
        "massStoreRun(): Blocking until task '{}' terminates ...",
        task.value().token));
    done->write_end = platform::FileDescriptor();
    platform::WaitUntilClosed(done->read_end);
  }
  return ApiResult<std::string>(task.value().token);
}

tasks::TaskResult<void> RunMassStoreTask(const tasks::QueuedTask& task,
                                         const tasks::StoreTaskInput& input,
                                         ServerContext* context) {
  Result<StoreOptions> options = ReadStoreOptions(
      util::pypath::Join({task.data_path, kConfigurationFile}));
  if (!options.ok()) {
    kLog.Error(
        "Invalid or unusable massStoreRunAsynchronous() configuration!");
    return TaskResult<void>(TaskFailure(options.error()));
  }
  const Result<db::QueryResult> rows = context->config_db().Query(
      "SELECT id, endpoint, display_name, connection, report_limit FROM "
      "products WHERE id = ?",
      {input.product_id});
  if (!rows.ok()) return TaskResult<void>(TaskFailure(rows.error()));
  if (rows.value().rows.empty()) {
    return TaskResult<void>(TaskFailure(util::KeyError(
        std::format("No product with ID '{}'", input.product_id))));
  }
  const db::Row& row = rows.value().rows.front();
  Product product({.id = row.Int(0),
                   .endpoint = row.Text(1),
                   .display_name = row.Text(2),
                   .connection = row.Text(3)},
                  context->workspace());
  product.Connect(/*init=*/false);
  Result<db::Connection*> db =
      product.db_status() == DBStatus::OK
          ? product.Connection()
          : Result<db::Connection*>(Error{});
  if (!db.ok()) {
    return TaskResult<void>(TaskError{
        .message = std::format("Database for product '{}' is in a bad shape!",
                               product.endpoint())});
  }
  MassStoreRun store(context, task.token, &product, db.value(),
                     std::move(options).value(),
                     util::pypath::Join({task.data_path, "store_zip"}),
                     row.Int(4));
  return store.Store(input.zip_size, input.preparation_seconds);
}

}  // namespace cc::server::api
