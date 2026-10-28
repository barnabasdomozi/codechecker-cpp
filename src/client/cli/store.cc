#include "client/cli/store.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/codeCheckerServersideTaskService.h"
#include "api/thrift/report_server_types.h"
#include "api/thrift/tasks_types.h"
#include "client/client.h"
#include "client/product.h"
#include "client/store/assemble_zip.h"
#include "client/task_client.h"
#include "common/checker_labels.h"
#include "common/cli/verbose.h"
#include "common/cmd_config.h"
#include "common/util.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/argparse.h"
#include "util/base64.h"
#include "util/compression.h"
#include "util/datetime.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/zip.h"
#include "web/cli/web_version.h"
#include "web/env.h"

namespace cc::client::cli {
namespace {

namespace argparse = util::argparse;

constexpr util::Logger kLog("system");

constexpr std::string_view kDescription = R"(
Store the results from one or more 'codechecker-analyze' result files in a
database.)";

constexpr std::string_view kEpilog = R"(
Environment variables
------------------------------------------------
  CC_PASSWORD      You can provide authentication credentials directly in this
                   variable. "codechecker-cpp cmd login" will use it as a
                   password or personal access token if set.

  CC_PASS_FILE     The location of the password file for auto login. By default
                   CodeChecker will use '~/.codechecker.passwords.json' file.
                   It can also be used to setup different credential files to
                   login to the same server with a different user.

  CC_SESSION_FILE  The location of the session file where valid sessions are
                   stored. This file will be automatically created by
                   CodeChecker. By default CodeChecker will use
                   '~/.codechecker.session.json'. This can be used if
                   restrictive permissions forbid CodeChecker from creating
                   files in the users home directory (e.g. in a CI
                   environment).


The results can be viewed by connecting to such a server in a Web browser or
via 'codechecker-cpp cmd'.)";

constexpr std::string_view kDetachHelp = R"(
Runs `store` in fire-and-forget mode: exit immediately once the server accepted
the analysis reports for storing, without waiting for the server-side data
processing to conclude.
Doing this is generally not recommended, as the client will never be notified
of potential processing failures, and there is no easy way to wait for the
successfully stored results to become available server-side for potential
further processing (e.g., `codechecker-cpp cmd diff`).
However, using '--detach' can significantly speed up large-scale monitoring
analyses where access to the results by a tool is not a goal, such as in the
case of non-gating CI systems.
)";

constexpr std::string_view kServerArgumentsDescription = R"(
Specifies a 'CodeChecker server' instance which will be used to store the
results. This server must be running and listening, and the given product must
exist prior to the 'store' command being ran.)";

// The largest compressed zip that is uploaded.
constexpr int64_t kMaxUploadSize = int64_t{1024} * 1024 * 1024;

// The name of a report folder whose metadata has no run name.
constexpr std::string_view kUnnamedFolder = "unnamed result folder";

// The time limit of the synchronous store (Python: _timeout_watchdog()).
constexpr std::chrono::seconds kSyncStoreTimeout{3600};

// The text of Python's TypeError when it compares a size with a limit that
// the server did not set.
constexpr std::string_view kNoLimit =
    "'>' not supported between instances of 'int' and 'NoneType'";

// The options of `store` that the upload needs.
struct StoreOptions {
  std::vector<std::string> inputs;  // Absolute paths.
  std::string run_name;
  std::optional<std::string> tag;
  std::optional<std::string> description;
  std::optional<std::vector<std::string>> trim_path_prefixes;
  bool force = false;
  bool detach = false;
  int jobs = 1;
};

// Returns the run name that the report folders in `inputs` were analyzed
// with (Python: __get_run_name()); nullopt if there is none.
std::optional<std::string> RunNameOfInputs(
    const std::vector<std::string>& inputs) {
  std::set<std::string> names;
  for (const std::string& input : inputs) {
    const std::string metadata = util::pypath::Join({input, "metadata.json"});
    if (!util::pypath::IsDir(input) || !util::pypath::Exists(metadata)) {
      continue;
    }
    const std::optional<util::JsonDocument> document =
        common::LoadJson(metadata);
    yyjson_val* root =
        document ? yyjson_doc_get_root(document->get()) : nullptr;
    std::optional<std::string> name;
    if (util::JsonToInt(util::JsonGet(root, "version")).value_or(0) >= 2) {
      // The run name of the last tool.
      yyjson_val* tools = util::JsonGet(root, "tools");
      if (const size_t count = yyjson_arr_size(tools); count > 0) {
        name = util::JsonOptionalString(
            util::JsonGet(yyjson_arr_get(tools, count - 1), "run_name"));
      }
    } else {
      name = util::JsonOptionalString(util::JsonGet(root, "name"));
    }
    names.insert(name && !name->empty() ? *name : std::string(kUnnamedFolder));
  }
  if (names.size() == 1) {
    if (*names.begin() == kUnnamedFolder) return std::nullopt;
    return *names.begin();
  }
  if (names.size() > 1) {
    return "multiple projects: " +
           util::Join(std::vector<std::string>(names.begin(), names.end()),
                      ", ");
  }
  return std::nullopt;
}

// Returns the error of a Python exception that is not caught, for the
// failed system call `error_number` on `path`: its text is printed as the
// last line of a traceback, so the command ends with exit code 1.
Error Uncaught(int error_number, const std::string& path) {
  std::string_view name = "OSError";
  if (error_number == ENOENT) name = "FileNotFoundError";
  if (error_number == ENOTDIR) name = "NotADirectoryError";
  util::Print(std::format("{}: {}", name,
                          util::OsError(error_number, path).message));
  return Error{};
}

// Returns a new folder for the temporary files of the store, in `dir`, or
// in the system's folder for temporary files if `dir` is not writable.
Result<std::string> MakeStoreTempDir(const std::string& dir) {
  std::error_code error;
  std::optional<std::string> temp_dir =
      platform::MakeTempDir(dir, "tmp", "-store", &error);
  if (!temp_dir) {
    if (error.value() != EACCES && error.value() != EPERM) {
      // Python does not handle this (known-issues #6).
      return Result<std::string>(Uncaught(error.value(), dir));
    }
    kLog.Debug(std::format(
        "'{}' is readonly, falling back to temporary folder", dir));
    temp_dir =
        platform::MakeTempDir(platform::TempDir(), "tmp", "-store", &error);
    if (!temp_dir) {
      return Result<std::string>(
          Error{.message = "Permission denied! You do not have sufficient "
                           "permissions to create the system temporary "
                           "directory."});
    }
  }
  kLog.Debug(std::format("{} directory created successfully!", *temp_dir));
  return Result<std::string>(std::move(*temp_dir));
}

// Returns the limit `kind` of `limits`; nullopt if the server did not set
// it.
std::optional<int64_t> LimitOf(
    const api::OrderedMap<StoreLimitKind::type, int64_t>& limits,
    StoreLimitKind::type kind) {
  const auto it = limits.find(kind);
  if (it == limits.end()) return std::nullopt;
  return it->second;
}

// Returns the files named `names` in the report folder `input` that the
// analysis statistics upload takes: the compilation database if it is
// within `limits`, the metadata and the compiler information.
Result<std::vector<std::string>> ReportFolderStatisticsFiles(
    const std::string& input, const std::vector<std::string>& names,
    const api::OrderedMap<StoreLimitKind::type, int64_t>& limits) {
  using Files = std::vector<std::string>;
  const bool has_compiler_info =
      std::ranges::find(names, "compiler_info.json") != names.end();
  Files files;
  for (const std::string& name : names) {
    const std::string path = util::pypath::Join({input, name});
    if (name == "compile_cmd.json") {
      const std::optional<int64_t> max =
          LimitOf(limits, StoreLimitKind::COMPILATION_DATABASE_SIZE);
      if (!max) return Result<Files>(Error{.message = std::string(kNoLimit)});
      if (platform::FileSize(path).value_or(0) > *max) {
        kLog.Debug(std::format("Compilation database is too big (max: {}).",
                               common::FormatSize(static_cast<double>(*max))));
      } else {
        kLog.Debug(std::format(
            "Copying file '{}' to analyzer statistics ZIP...", path));
        files.push_back(path);
      }
    } else if (name == "metadata.json" || name == "compiler_info.json" ||
               ((name == "compiler_includes.json" ||
                 name == "compiler_target.json") &&
                !has_compiler_info)) {
      files.push_back(path);
    }
  }
  return Result<Files>(std::move(files));
}

// Returns the failure zips in the folder `failed_dir`, as many as fit in
// the size limit of `limits` together.
Result<std::vector<std::string>> FailureZips(
    const std::string& failed_dir,
    const api::OrderedMap<StoreLimitKind::type, int64_t>& limits) {
  using Files = std::vector<std::string>;
  Files files;
  int64_t total_size = 0;
  for (const std::string& name : platform::ListDirectory(failed_dir)) {
    const std::string path = util::pypath::Join({failed_dir, name});
    if (util::pypath::IsDir(path)) continue;
    const std::optional<int64_t> max =
        LimitOf(limits, StoreLimitKind::FAILURE_ZIP_SIZE);
    if (!max) return Result<Files>(Error{.message = std::string(kNoLimit)});
    total_size += platform::FileSize(path).value_or(0);
    if (total_size > *max) {
      kLog.Debug(std::format("We reached the limit of maximum uploadable "
                             "failure zip size (max: {}).",
                             common::FormatSize(static_cast<double>(*max))));
      break;
    }
    kLog.Debug(std::format(
        "Copying failure zip file '{}' to analyzer statistics ZIP...", path));
    files.push_back(path);
  }
  return Result<Files>(std::move(files));
}

// Returns the files of the report folder `input` that the analysis
// statistics upload takes (Python: get_analysis_statistics()): its
// compilation database and compiler information, and the failure zips
// within `limits`. None if there is no failure zip. A limit that the server
// did not set fails as in Python.
Result<std::vector<std::string>> AnalysisStatisticsFiles(
    const std::string& input,
    const api::OrderedMap<StoreLimitKind::type, int64_t>& limits) {
  using Files = std::vector<std::string>;
  std::vector<std::string> dirs;
  std::vector<std::string> names;
  if (!util::pypath::IsFile(input)) {
    for (std::string& name : platform::ListDirectory(input)) {
      if (util::pypath::IsDir(util::pypath::Join({input, name}))) {
        dirs.push_back(std::move(name));
      } else {
        names.push_back(std::move(name));
      }
    }
  }
  Result<Files> files = ReportFolderStatisticsFiles(input, names, limits);
  if (!files.ok()) return files;
  if (std::ranges::find(dirs, "failed") == dirs.end()) {
    return Result<Files>(Files());
  }
  const Result<Files> failure_zips =
      FailureZips(util::pypath::Join({input, "failed"}), limits);
  if (!failure_zips.ok()) return failure_zips;
  if (failure_zips.value().empty()) return Result<Files>(Files());
  files.value().insert(files.value().end(), failure_zips.value().begin(),
                       failure_zips.value().end());
  return files;
}

// Writes `files` into the zip `zip_file`, under their paths without the
// leading '/', and compresses it with zlib. Returns the Base64 of the
// result.
Result<std::string> StatisticsZip(const std::vector<std::string>& files,
                                  const std::string& zip_file) {
  Result<util::ZipArchive> zip =
      util::ZipArchive::Open(zip_file, util::ZipArchive::Mode::kWrite);
  if (!zip.ok()) return Result<std::string>(zip.error());
  for (const std::string& file : files) {
    const Result<void> added =
        zip.value().AddFile(std::string(util::LeftStrip(file, "/")), file,
                            util::ZipArchive::Compression::kStored);
    if (!added.ok()) return Result<std::string>(added.error());
  }
  if (const Result<void> closed = zip.value().Close(); !closed.ok()) {
    return Result<std::string>(closed.error());
  }
  const Result<std::string> compressed = util::ZlibCompressFile(zip_file);
  if (!compressed.ok()) return compressed;
  kLog.Debug(std::format("[ZIP] Analysis statistics zip written at '{}'",
                         zip_file));
  return Result<std::string>(util::Base64Encode(compressed.value()));
}

// Uploads the analysis statistics of `inputs` (only the first one, as in
// Python) for the run `run_name` (Python: store_analysis_statistics()).
// Errors are only logged at debug level, except failed API calls.
Result<void> StoreAnalysisStatistics(const std::vector<std::string>& inputs,
                                     const std::string& run_name,
                                     ResultsClient& results) {
  const std::optional<std::string> zip_file =
      platform::MakeTempFile(platform::TempDir(), ".zip");
  if (!zip_file) {
    return Result<void>(Error{.message = "Cannot create a temporary file."});
  }
  kLog.Debug(std::format("Will write failed store ZIP to '{}'...", *zip_file));
  const auto upload = [&]() -> Result<void> {
    api::OrderedMap<StoreLimitKind::type, int64_t> limits;
    Result<void> called = results.Call(
        "getAnalysisStatisticsLimits",
        [&limits](codeCheckerDBAccessClient& client) {
          client.getAnalysisStatisticsLimits(limits);
        });
    if (!called.ok()) return called;
    const Result<std::vector<std::string>> files =
        inputs.empty() ? Result<std::vector<std::string>>(
                             std::vector<std::string>())
                       : AnalysisStatisticsFiles(inputs.front(), limits);
    if (!files.ok()) return Result<void>(files.error());
    if (files.value().empty()) {
      kLog.Debug("No analyzer statistics information can be found in the "
                 "report directory.");
      return Result<void>();
    }
    const Result<std::string> zip = StatisticsZip(files.value(), *zip_file);
    if (!zip.ok()) return Result<void>(zip.error());
    return results.Call(
        "storeAnalysisStatistics", [&](codeCheckerDBAccessClient& client) {
          client.storeAnalysisStatistics(run_name, zip.value());
        });
  };
  const Result<void> uploaded = upload();
  platform::RemoveTree(*zip_file);  // Removes the file.
  if (!uploaded.ok() && !uploaded.error().message.empty()) {
    kLog.Debug(std::format("Storage of analysis statistics zip has been "
                           "failed: {}",
                           uploaded.error().message));
    return Result<void>();
  }
  return uploaded;
}

// Uploads the statistics of the analysis if the server of `results` keeps
// them.
Result<void> StoreAnalysisStatisticsIfAllowed(const StoreOptions& options,
                                              ResultsClient& results) {
  bool allowed = false;
  const Result<void> called = results.Call(
      "allowsStoringAnalysisStatistics",
      [&allowed](codeCheckerDBAccessClient& client) {
        allowed = client.allowsStoringAnalysisStatistics();
      });
  if (!called.ok() || !allowed) return called;
  return StoreAnalysisStatistics(options.inputs, options.run_name, results);
}

// Stores `zip` with the legacy massStoreRun(), which returns when the server
// finished, within an hour (Python: the CC_FORCE_SYNC_STORE path).
Result<void> StoreSynchronously(const StoreOptions& options,
                                const std::string& version,
                                const std::string& zip,
                                ResultsClient& results) {
  results.connection().set_timeout(kSyncStoreTimeout);
  const Result<void> stored = results.Call(
      "massStoreRun", [&](codeCheckerDBAccessClient& client) {
        client.massStoreRun(options.run_name, options.tag.value_or(""),
                            version, zip, options.force,
                            options.trim_path_prefixes.value_or(
                                std::vector<std::string>()),
                            options.description.value_or(""));
      });
  results.connection().set_timeout(std::nullopt);
  if (!stored.ok() && !stored.error().message.empty()) {
    const int64_t timeout =
        kSyncStoreTimeout.count() * util::kMicrosecondsPerSecond;
    const std::string duration = util::FormatTimedelta(timeout);
    const double seconds = static_cast<double>(kSyncStoreTimeout.count());
    kLog.Warning(std::format("Timeout watchdog hit {} seconds ({})",
                             util::FloatRepr(seconds), duration));
    kLog.Error(std::format(
        "Timeout!\n\tThe server's reply did not arrive after {} seconds ({}) "
        "elapsed since the server-side processing began.\n\n\tThis does "
        "*NOT* mean that there was an issue with the run you were storing!"
        "\n\tThe server might still be processing the results...\n\tHowever, "
        "it is more likely that the server had already finished, but the "
        "client did not receive a response.\n\tUsually, this is caused by "
        "the underlying TCP connection failing to signal a low-level "
        "disconnect.\n\tClients potentially hanging indefinitely in these "
        "scenarios is an unfortunate and known issue.\n\t\tSee "
        "http://github.com/Ericsson/codechecker/issues/3672 for details!\n\n"
        "\tThis error here is a temporary measure to ensure an infinite "
        "hang is replaced with a well-explained timeout.\n\tA more proper "
        "solution will be implemented in a subsequent version of "
        "CodeChecker.",
        kSyncStoreTimeout.count(), duration));
    return Result<void>(Error{});
  }
  if (!stored.ok()) return stored;
  return StoreAnalysisStatisticsIfAllowed(options, results);
}

// Returns the options of massStoreRunAsynchronous() for `options` and the
// client version `version`.
SubmittedRunOptions ToSubmittedRunOptions(const StoreOptions& options,
                                          const std::string& version) {
  SubmittedRunOptions submitted;
  submitted.__set_runName(options.run_name);
  if (options.tag) submitted.__set_tag(*options.tag);
  submitted.__set_version(version);
  submitted.__set_force(options.force);
  if (options.trim_path_prefixes) {
    submitted.__set_trimPathPrefixes(*options.trim_path_prefixes);
  }
  if (options.description) submitted.__set_description(*options.description);
  return submitted;
}

// Waits until the server of `url` stored the run of the task `token`, and
// fails, after logging why, if the task did not complete.
Result<void> AwaitStore(const ProductUrl& url, const std::string& token) {
  Result<TaskClient> tasks = SetupTaskClient(url.protocol, url.host, url.port);
  if (!tasks.ok()) return Result<void>(tasks.error());
  const Result<std::string> status =
      AwaitTaskTermination(token, tasks.value());
  if (!status.ok()) return Result<void>(status.error());
  if (status.value() == "COMPLETED") {
    kLog.Info("Storing the reports finished successfully.");
    return Result<void>();
  }
  TaskInfo info;
  const Result<void> called = tasks.value().Call(
      "getTaskInfo", [&](codeCheckerServersideTaskServiceClient& client) {
        client.getTaskInfo(info, token);
      });
  if (!called.ok()) return called;
  kLog.Error(std::format(
      "Storing the reports failed! The job terminated in status '{}'. The "
      "comments associated with the failure are:\n\n{}",
      status.value(), info.comments));
  return Result<void>(Error{});
}

// Submits `zip` for storing with massStoreRunAsynchronous() and, unless
// --detach was given, waits until the server stored it.
Result<void> StoreAsynchronously(const ProductUrl& url,
                                 const StoreOptions& options,
                                 const std::string& version,
                                 const std::string& zip,
                                 ResultsClient& results) {
  const SubmittedRunOptions submitted = ToSubmittedRunOptions(options, version);
  std::string token;
  Result<void> done = results.Call(
      "massStoreRunAsynchronous", [&](codeCheckerDBAccessClient& client) {
        client.massStoreRunAsynchronous(token, zip, submitted);
      });
  if (!done.ok()) return done;
  kLog.Info("Reports submitted to the server for processing.");
  done = StoreAnalysisStatisticsIfAllowed(options, results);
  if (!done.ok()) return done;
  if (!options.detach) return AwaitStore(url, token);
  kLog.Warning(std::format(
      "Exiting the 'store' subcommand as '--detach' was specified: not "
      "waiting for the result of the store operation.\nThe server might "
      "not have finished processing everything at this point, so do NOT "
      "rely on querying the results just yet!\nTo await the completion of "
      "the processing later, you can execute:\n\n\tCodeChecker cmd "
      "serverside-tasks --token {} --await",
      token));
  // Scripts read the token from stdout.
  util::Print(token);
  return Result<void>();
}

// Builds the zip `zip_file` from the inputs and uploads it (the `try` part
// of Python's main()). `temp_dir` is where the unique result files go.
Result<void> AssembleAndUpload(const StoreOptions& options,
                               const ProductUrl& url,
                               const std::string& temp_dir,
                               const std::string& zip_file,
                               ResultsClient& results,
                               ProductClient& products) {
  const Result<common::VersionInfo> version = web::cli::ReadWebVersion();
  if (!version.ok()) return Result<void>(version.error());
  const Result<common::CheckerLabels> labels =
      common::CheckerLabels::Load(common::LabelsDirectory());
  if (!labels.ok()) return Result<void>(labels.error());
  kLog.Debug("Assembling zip file.");
  const Result<void> assembled =
      options.jobs < 1
          ? Result<void>(Error{.message = "max_workers must be greater than 0"})
          : store::AssembleZip(options.inputs, zip_file, labels.value(),
                               temp_dir, options.jobs, results, products);
  if (!assembled.ok()) {
    if (assembled.error().message.empty()) return assembled;
    util::Print(assembled.error().message);
    kLog.Error("Failed to assemble zip file.");
    return Result<void>(Error{});
  }
  const Result<std::string> zip = util::ReadFile(zip_file);
  if (!zip.ok()) return Result<void>(zip.error());
  const int64_t zip_size = static_cast<int64_t>(zip.value().size());
  if (zip_size > kMaxUploadSize) {
    kLog.Error(std::format(
        "The result list to upload is too big (max: {}): {}.",
        common::FormatSize(static_cast<double>(kMaxUploadSize)),
        common::FormatSize(static_cast<double>(zip_size))));
    return Result<void>(Error{});
  }
  const std::string b64zip = util::Base64Encode(zip.value());
  if (b64zip.empty()) {
    kLog.Info("Zip content is empty, nothing to store!");
    return Result<void>(Error{});
  }
  kLog.Info("Storing results to the server ...");
  if (common::StrToBool(
          platform::GetEnv("CC_FORCE_SYNC_STORE").value_or("no"))) {
    return StoreSynchronously(options, version.value().version, b64zip,
                              results);
  }
  return StoreAsynchronously(url, options, version.value().version, b64zip,
                             results);
}

// Returns the options of the upload in `args`, with the run name; fails if
// an input is missing or no run name can be found.
Result<StoreOptions> ToStoreOptions(const argparse::Namespace& args) {
  StoreOptions options;
  for (const std::string& input : args.GetList("input")) {
    options.inputs.push_back(util::pypath::Abspath(input));
  }
  for (const std::string& input : options.inputs) {
    if (!util::pypath::Exists(input)) {
      return Result<StoreOptions>(Error{
          .message = std::format("Input path '{}' does not exist!", input)});
    }
  }
  if (args.Has("name")) {
    options.run_name = args.Get("name");
  } else {
    kLog.Debug("Generating name for analysis...");
    std::optional<std::string> name = RunNameOfInputs(options.inputs);
    if (!name) {
      return Result<StoreOptions>(Error{
          .message = "No suitable name was found in the inputs for the "
                     "analysis run. Please specify one by passing argument "
                     "--name run_name in the invocation.",
          .exit_code = 2});
    }
    options.run_name = std::move(*name);
  }
  const auto optional = [&args](std::string_view dest) {
    return args.Has(dest) ? std::optional<std::string>(args.Get(dest))
                          : std::nullopt;
  };
  options.tag = optional("tag");
  options.description = optional("description");
  if (args.Has("trim_path_prefix")) {
    options.trim_path_prefixes = args.GetList("trim_path_prefix");
  }
  options.force = args.Has("force");
  options.detach = args.Has("detach");
  options.jobs = static_cast<int>(*util::ParseInt(args.Get("jobs")));
  return Result<StoreOptions>(std::move(options));
}

// Python: main() of store.py, after the logger is set up.
Result<void> Store(const argparse::Namespace& args) {
  if (const Result<void> checked = common::CheckConfigFile(args);
      !checked.ok()) {
    return checked;
  }
  const Result<StoreOptions> options = ToStoreOptions(args);
  if (!options.ok()) return Result<void>(options.error());
  kLog.Info(std::format("Storing analysis results for run '{}'",
                        options.value().run_name));
  if (options.value().force) {
    kLog.Info(std::format("argument --force was specified: the run with name "
                          "'{}' will be deleted",
                          options.value().run_name));
  }

  const std::string& product_url = args.Get("product_url");
  Result<ResultsClient> results = SetupClient(product_url);
  if (!results.ok()) return Result<void>(results.error());
  const ProductUrl url = *SplitProductUrl(product_url);
  Result<ProductClient> products = SetupProductClient(url);
  if (!products.ok()) return Result<void>(products.error());

  // The temporary files go next to the results, unless --zip-loc says
  // otherwise.
  const std::string temp_dir_path =
      args.Has("zip_loc") ? util::pypath::Abspath(args.Get("zip_loc"))
                          : options.value().inputs.front();
  const Result<std::string> temp_dir = MakeStoreTempDir(temp_dir_path);
  if (!temp_dir.ok()) return Result<void>(temp_dir.error());
  const std::optional<std::string> zip_file =
      platform::MakeTempFile(temp_dir.value(), ".zip");
  if (!zip_file) {
    platform::RemoveTree(temp_dir.value());
    return Result<void>(Error{.message = std::format(
                                  "Cannot create a temporary file in '{}'.",
                                  temp_dir.value())});
  }
  kLog.Debug(std::format("Will write mass store ZIP to '{}'...", *zip_file));
  Result<void> stored =
      AssembleAndUpload(options.value(), url, temp_dir_path, *zip_file,
                        results.value(), products.value());
  platform::RemoveTree(temp_dir.value());
  if (!stored.ok() && !stored.error().message.empty()) {
    stored = Result<void>(Error{.message = "Storing the reports failed: " +
                                           stored.error().message});
  }
  return stored;
}

}  // namespace

void AddStoreArguments(argparse::Parser& parser) {
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));
  parser.set_epilog(std::string(kEpilog));

  parser.AddArgument(
      {.dest = "input",
       .nargs = argparse::Nargs::kZeroOrMore,
       .metavar = "file/folder",
       .default_value =
           util::pypath::Join({web::DefaultWorkspace(), "reports"}),
       .help = "The analysis result files and/or folders containing analysis "
               "results which should be parsed and printed. If multiple "
               "report directories are given, OFF and UNAVAILABLE detection "
               "statuses will not be available."});
  parser.AddArgument({.flags = {"-t", "--type", "--input-format"},
                      .dest = "input_format",
                      .choices = {"plist"},
                      .default_value = "plist",
                      .help = "Specify the format the analysis results were "
                              "created as."});
  parser.AddArgument(
      {.flags = {"-n", "--name"},
       .dest = "name",
       .help = "The name of the analysis run to use in storing the reports to "
               "the database. If not specified, the '--name' parameter given "
               "to 'codechecker-analyze' will be used, if exists."});
  parser.AddArgument({.flags = {"--tag"},
                      .dest = "tag",
                      .help = "A uniques identifier for this individual store "
                              "of results in the run's history."});
  parser.AddArgument({.flags = {"--description"},
                      .dest = "description",
                      .help = "A custom textual description to be shown "
                              "alongside the run."});
  parser.AddArgument(
      {.flags = {"--trim-path-prefix"},
       .dest = "trim_path_prefix",
       .nargs = argparse::Nargs::kZeroOrMore,
       .help = "Removes leading path from files which will be printed. For "
               "instance if you analyze files '/home/jsmith/my_proj/x.cpp' "
               "and '/home/jsmith/my_proj/y.cpp', but would prefer to have "
               "them displayed as 'my_proj/x.cpp' and 'my_proj/y.cpp' in the "
               "web/CLI interface, invoke CodeChecker with "
               "'--trim-path-prefix \"/home/jsmith/\"'.If multiple prefixes "
               "are given, the longest match will be removed. You may also "
               "use Unix shell-like wildcards (e.g. '/*/jsmith/')."});
  parser.AddArgument(
      {.flags = {"-j", "--jobs"},
       .dest = "jobs",
       .type = argparse::IntType,
       .default_value = std::to_string(platform::CpuCount()),
       .help = "Number of parallel jobs that process the input directory to "
               "upload. If the directory is located on NFS drive, then the "
               "storage may hang indefinitely in case of parallel "
               "processing. Choosing value 1 doesn't use sub-processes."});
  parser.AddArgument(
      {.flags = {"--zip-loc"},
       .dest = "zip_loc",
       .metavar = "PATH",
       .default_none = true,
       .help = "Specify the location to write the compressed file used for "
               "storage. Useful if the results directory is read only. "
               "Defaults to the results directory."});
  parser.AddArgument({.flags = {"--detach"},
                      .dest = "detach",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .help = std::string(kDetachHelp)});
  common::AddConfigOption(parser, /*group=*/0);
  parser.AddArgument(
      {.flags = {"-f", "--force"},
       .dest = "force",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Delete analysis results stored in the database for the "
               "current analysis run's name and store only the results "
               "reported in the 'input' files. (By default, CodeChecker would "
               "keep reports that were coming from files not affected by the "
               "analysis, and only incrementally update defect reports for "
               "source files that were analysed.)"});

  const int server_group = parser.AddArgumentGroup(
      "server arguments", std::string(kServerArgumentsDescription));
  parser.AddArgument({.flags = {"--url"},
                      .dest = "product_url",
                      .metavar = "PRODUCT_URL",
                      .default_value = "localhost:8001/Default",
                      .help = "The URL of the product to store the results "
                              "for, in the format of "
                              "'[http[s]://]host:port/Endpoint'.",
                      .group = server_group});
  common::cli::AddVerboseArgument(parser);
}

int RunStore(const argparse::Namespace& args) {
  util::SetupLogger(common::cli::Verbose(args), util::Stream::kStdout);
  const Result<void> stored = Store(args);
  if (stored.ok()) return 0;
  if (!stored.error().message.empty()) kLog.Error(stored.error().message);
  return stored.error().exit_code;
}

}  // namespace cc::client::cli
