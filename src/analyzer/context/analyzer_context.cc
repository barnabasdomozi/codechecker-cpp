#include "analyzer/context/analyzer_context.h"

#include <yyjson.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "common/checker_labels.h"
#include "common/package.h"
#include "common/util.h"
#include "common/version.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("system");

// Loads a JSON file of the package's config folder; an error if it is
// missing or empty (Python: __get_package_config() and
// __get_package_layout()).
Result<util::JsonDocument> LoadPackageConfig(const std::string& path) {
  std::optional<util::JsonDocument> document = common::LoadJson(path);
  if (!document || !util::JsonTruthy(yyjson_doc_get_root(document->get()))) {
    kLog.Error(std::format(
        "No configuration file '{}' can be found or it is empty!", path));
    return Result<util::JsonDocument>(Error{});
  }
  return Result<util::JsonDocument>(std::move(*document));
}

// Returns the folders of the list `name` of the package layout, joined to
// the package root; none when the analyzers come from PATH.
std::vector<std::string> ExtraPaths(yyjson_val* layout, std::string_view name,
                                    const std::string& data_files_dir) {
  std::vector<std::string> paths;
  if (IsAnalyzerFromPath()) return paths;
  yyjson_arr_iter iter = yyjson_arr_iter_with(util::JsonGet(layout, name));
  while (yyjson_val* path = yyjson_arr_iter_next(&iter)) {
    paths.push_back(
        util::pypath::Join({data_files_dir, util::JsonToStr(path)}));
  }
  return paths;
}

// Returns the environment saved by the launcher (Python:
// env.get_original_env()), or the current one if there is none.
Environment OriginalEnvironment() {
  Result<std::vector<std::string>> env = ReadOriginalEnvironment();
  if (!env.ok()) {
    kLog.Warning(env.error().message);
    kLog.Warning("Failed to get saved original_env ");
    return std::nullopt;
  }
  return std::move(env).value();
}

// Returns the architecture folders of ld_logger/lib, for example
// ".../lib/32bit" and ".../lib/64bit", in the order the system lists them
// (Python: Context.logger_lib_dir_path).
std::vector<std::string> LoggerLibFolders() {
  const std::vector<std::string> roots = LoggerLibRoots();
  const std::string& root =
      util::pypath::IsDir(roots[0]) ? roots[0] : roots[1];
  std::vector<std::string> folders;
  for (const std::string& name : platform::ListDirectory(root)) {
    std::string folder = util::pypath::Join({root, name});
    if (util::pypath::IsDir(folder)) folders.push_back(std::move(folder));
  }
  return folders;
}

// Returns the path of ldlogger.so in the folder of `folders` that matches
// this program's architecture. If there is none, it warns and returns the
// path in the first folder; nullopt if `folders` is empty.
std::optional<std::string> AbsoluteLoggerLibPath(
    const std::vector<std::string>& folders) {
  const std::string_view arch_dir = sizeof(void*) == 8 ? "64bit" : "32bit";
  const auto folder =
      std::ranges::find_if(folders, [arch_dir](const std::string& path) {
        return path.ends_with(arch_dir);
      });
  if (folder != folders.end()) {
    return util::pypath::Join({*folder, kLoggerLibName});
  }
  if (folders.empty()) return std::nullopt;
  kLog.Warning(std::format(
      "Could not find current architecture \"{}\" in list of potential "
      "paths \"{}\", falling back to first architecture path \"{}\"!",
      arch_dir, util::Join(folders, ":"), folders[0]));
  return util::pypath::Join({folders[0], kLoggerLibName});
}

// Parses CC_ANALYZER_BIN (Python: __parse_cc_analyzer_bin()). Reports the
// problems it finds.
Result<std::map<std::string, std::string>> ParseAnalyzerBinVariable() {
  using Binaries = Result<std::map<std::string, std::string>>;
  std::map<std::string, std::string> binaries;
  const std::optional<std::string> variable =
      platform::GetEnv("CC_ANALYZER_BIN");
  if (!variable) return Binaries(std::move(binaries));
  static const util::Regex& kItem =
      *new util::Regex("(?P<analyzer>.+):(?P<path>.+)");
  bool had_error = false;
  for (const std::string& value : util::Split(*variable, ";")) {
    const std::optional<util::RegexMatch> match = kItem.Match(value);
    if (!match) {
      kLog.Error(std::format("Analyzer binary in wrong format: {}, should be "
                             "<analyzer>:</path/to/bin/>",
                             value));
      had_error = true;
      continue;
    }
    const std::string name(*match->Group("analyzer"));
    const std::string path(*match->Group("path"));
    const std::optional<std::string> resolved =
        platform::Which(path, std::nullopt);
    if (!resolved) {
      // Python calls os.path.isfile(None), which raises.
      util::Print("TypeError: stat: path should be string, bytes, os.PathLike "
                  "or integer, not NoneType");
      return Binaries(Error{});
    }
    if (!util::pypath::IsFile(*resolved)) {
      kLog.Error(std::format(
          "'{}' is not a path to an analyzer binary given to CC_ANALYZER_BIN!",
          path));
      had_error = true;
    }
    // Python checks the flag of all items, so one error skips the rest.
    if (had_error) continue;
    kLog.Info(std::format("Using '{}' for analyzer '{}'", *resolved, name));
    binaries[name] = *resolved;
  }
  if (had_error) {
    kLog.Info("The value of CC_ANALYZER_BIN should be in theformat of "
              "CC_ANALYZER_BIN='<analyzer1>:/path/to/bin1;<analyzer2>:/path/"
              "to/bin2'");
    return Binaries(Error{});
  }
  return Binaries(std::move(binaries));
}

}  // namespace

std::optional<std::string> AnalyzerContext::AnalyzerBinary(
    std::string_view name) const {
  const auto binary = analyzer_binaries_.find(name);
  if (binary == analyzer_binaries_.end()) return std::nullopt;
  return binary->second;
}

void AnalyzerContext::SetAnalyzerBinary(std::string_view name,
                                        const std::string& binary) {
  analyzer_binaries_[std::string(name)] = binary;
}

Environment AnalyzerContext::EnvForBin(const std::string& binary) const {
  const std::string resolved = util::pypath::Realpath(binary);
  if (!util::pypath::Exists(resolved)) {
    kLog.Error("Binary " + binary + " not found");
    return std::nullopt;
  }
  if (resolved.starts_with(data_files_dir_)) {
    kLog.Debug("Package env is returned for " + resolved);
    return package_env_;
  }
  kLog.Debug("Original env is returned for " + resolved);
  return original_env_;
}

std::optional<std::string> AnalyzerContext::CheckerPluginDir() const {
  if (IsAnalyzerFromPath()) return std::nullopt;
  return util::pypath::Join({data_files_dir_, "plugin"});
}

Result<void> AnalyzerContext::PopulateAnalyzers(
    const std::vector<std::pair<std::string, std::string>>& configured) {
  const Result<std::map<std::string, std::string>> from_variable =
      ParseAnalyzerBinVariable();
  if (!from_variable.ok()) return Result<void>(from_variable.error());
  const bool from_path = IsAnalyzerFromPath();
  for (const auto& [name, configured_value] : configured) {
    if (const auto it = from_variable.value().find(name);
        it != from_variable.value().end()) {
      analyzer_binaries_[name] = it->second;
      continue;
    }
    const std::string value =
        from_path ? util::pypath::Basename(configured_value) : configured_value;
    if (!util::pypath::Dirname(value).empty()) {
      analyzer_binaries_[name] = util::pypath::Join({data_files_dir_, value});
      continue;
    }
    const std::optional<std::string> found =
        platform::Which(value, std::nullopt);
    if (!found) {
      kLog.Debug(std::format("'{}' binary can not be found in your PATH!",
                             value));
      analyzer_binaries_[name] = std::nullopt;
      continue;
    }
    // A symbolic link to ccache is kept, so ccache finds the compiler.
    const std::string real = util::pypath::Realpath(*found);
    analyzer_binaries_[name] = real.ends_with("/ccache") ? *found : real;
  }
  return Result<void>();
}

Result<AnalyzerContext*> GetContext() {
  // Python: Context.__init__(), run once.
  static const Result<AnalyzerContext*>& context =
      *new Result<AnalyzerContext*>([]() -> Result<AnalyzerContext*> {
        const std::string data_files_dir = common::DataFilesDir();
        const std::string config_dir =
            util::pypath::Join({data_files_dir, "config"});
        const Result<util::JsonDocument> config = LoadPackageConfig(
            util::pypath::Join({config_dir, "config.json"}));
        if (!config.ok()) return Result<AnalyzerContext*>(config.error());
        const Result<util::JsonDocument> layout = LoadPackageConfig(
            util::pypath::Join({config_dir, "package_layout.json"}));
        if (!layout.ok()) return Result<AnalyzerContext*>(layout.error());
        Result<common::CheckerLabels> labels =
            common::CheckerLabels::Load(common::LabelsDirectory());
        if (!labels.ok()) {
          kLog.Error(labels.error().message);
          return Result<AnalyzerContext*>(Error{});
        }
        auto* result =
            new AnalyzerContext(data_files_dir, std::move(labels).value());
        yyjson_val* layout_root = yyjson_doc_get_root(layout.value().get());
        result->path_env_extra_ =
            ExtraPaths(layout_root, "path_env_extra", data_files_dir);
        result->ld_lib_path_extra_ =
            ExtraPaths(layout_root, "ld_lib_path_extra", data_files_dir);
        result->original_env_ = OriginalEnvironment();
        result->package_env_ = platform::CurrentEnvironment();
        PrependPaths("PATH", result->path_env_extra_, result->package_env_);
        PrependPaths("LD_LIBRARY_PATH", result->ld_lib_path_extra_,
                     result->package_env_);

        const Result<common::VersionInfo> version = common::ReadVersionFile(
            util::pypath::Join({config_dir, "analyzer_version.json"}));
        if (!version.ok()) return Result<AnalyzerContext*>(version.error());
        result->version_ = version.value().version;
        result->git_tag_ = version.value().git_tag;
        result->git_hash_ = version.value().git_hash;

        std::vector<std::pair<std::string, std::string>> configured;
        yyjson_obj_iter iter = yyjson_obj_iter_with(util::JsonGet(
            util::JsonGet(layout_root, "runtime"), "analyzers"));
        while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
          configured.emplace_back(
              util::JsonToStr(key),
              util::JsonToStr(yyjson_obj_iter_get_val(key)));
        }
        if (Result<void> populated = result->PopulateAnalyzers(configured);
            !populated.ok()) {
          return Result<AnalyzerContext*>(populated.error());
        }
        return Result<AnalyzerContext*>(result);
      }());
  return context;
}

Result<std::vector<std::string>> ReadOriginalEnvironment() {
  const std::optional<std::string> file =
      platform::GetEnv("CODECHECKER_ORIGINAL_BUILD_ENV");
  if (!file || file->empty()) {
    return Result<std::vector<std::string>>(platform::CurrentEnvironment());
  }
  kLog.Debug("Loading original build env from: " + *file);
  const Result<std::string> content = util::ReadFile(*file);
  if (!content.ok()) return Result<std::vector<std::string>>(content.error());
  std::vector<std::string> env;
  for (std::string& item : util::Split(content.value(), std::string(1, '\0'))) {
    if (!item.empty()) env.push_back(std::move(item));
  }
  return Result<std::vector<std::string>>(std::move(env));
}

void PrependPaths(const std::string& name,
                  const std::vector<std::string>& extra,
                  std::vector<std::string>& env) {
  if (extra.empty()) return;
  const std::string joined = util::Join(extra, ":");
  const std::optional<std::string> value = platform::GetEnv(env, name);
  platform::SetEnv(&env, name, value ? joined + ":" + *value : joined);
}

std::vector<std::string> LoggerLibRoots() {
  return {
      util::pypath::Join(
          {platform::GetEnv("CC_DATA_FILES_DIR").value_or(""), "ld_logger",
           "lib"}),
      util::pypath::Join({platform::GetEnv("CC_LIB_DIR").value_or(""),
                          "codechecker_analyzer", "ld_logger", "lib"})};
}

void SetLogEnvironment(const std::string& logfile,
                       bool use_absolute_ldpreload_path,
                       std::vector<std::string>& env) {
  platform::SetEnv(
      &env, "CC_LOGGER_BIN",
      util::pypath::Join(
          {platform::GetEnv("CC_BIN_DIR").value_or(""), "ld_logger"}));
  const std::vector<std::string> folders = LoggerLibFolders();
  std::optional<std::string> ldlogger_path;
  if (use_absolute_ldpreload_path) {
    kLog.Debug("Trying to use absolute paths for ldlogger.so library");
    ldlogger_path = AbsoluteLoggerLibPath(folders);
  }

  std::string preload_value;
  if (ldlogger_path) {
    kLog.Debug(std::format(
        "Successfully found absolute path for ldlogger.so library: \"{}\"",
        *ldlogger_path));
    preload_value = *ldlogger_path;
  } else {
    kLog.Debug(
        "Using LD_LIBRARY_PATH environment variable and a relative path to "
        "load ldlogger.so library");
    preload_value = kLoggerLibName;
    PrependPaths("LD_LIBRARY_PATH", folders, env);
    kLog.Debug(std::format(
        "LD_LIBRARY_PATH environment variable set to: \"{}\"",
        platform::GetEnv(env, "LD_LIBRARY_PATH").value_or("")));
  }

  const std::optional<std::string> preload =
      platform::GetEnv(env, "LD_PRELOAD");
  platform::SetEnv(&env, "LD_PRELOAD",
                   preload && !util::Strip(*preload).empty()
                       ? *preload + " " + preload_value
                       : preload_value);
  kLog.Debug(std::format("LD_PRELOAD environment variable set to: \"{}\"",
                         platform::GetEnv(env, "LD_PRELOAD").value_or("")));
  platform::SetEnv(&env, "CC_LOGGER_FILE", logfile);
}

Environment EnvForBinary(const std::string& binary) {
  const Result<AnalyzerContext*> context = GetContext();
  return context.ok() ? context.value()->EnvForBin(binary) : std::nullopt;
}

bool IsAnalyzerFromPath() {
  const std::string value =
      util::AsciiLower(platform::GetEnv("CC_ANALYZERS_FROM_PATH").value_or(""));
  return value == "yes" || value == "1";
}

std::optional<std::string> GetBinaryInPath(
    const std::vector<std::string>& basenames,
    std::string_view versioning_pattern, const Environment& env) {
  const util::Regex regex(versioning_pattern);
  const std::string path =
      (env ? platform::GetEnv(*env, "PATH") : platform::GetEnv("PATH"))
          .value_or("");
  // File name -> its paths, in PATH order.
  std::map<std::string, std::vector<std::string>> binaries;
  for (const std::string& folder : util::Split(path, ":")) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end;
         !error && it != end; it.increment(error)) {
      std::error_code status_error;
      // os.walk() lists folders apart; links to files are files.
      if (it->is_directory(status_error)) continue;
      const std::string name = it->path().filename().string();
      if (regex.Match(name)) {
        binaries[name].push_back(util::pypath::Join({folder, name}));
      }
    }
  }
  if (binaries.empty()) return std::nullopt;
  if (binaries.size() == 1) return binaries.begin()->second.front();
  for (const std::string& basename : basenames) {
    if (const auto it = binaries.find(basename); it != binaries.end()) {
      return it->second.front();
    }
  }
  // The "newest" version: the alphabetically last name.
  return binaries.rbegin()->second.front();
}

}  // namespace cc::analyzer
