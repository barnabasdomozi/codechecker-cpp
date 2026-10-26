// Port of codechecker_analyzer/analyzer_context.py and env.py: the package
// configuration, the checker labels, and where the analyzers are and in
// which environment they run (analyzers.md §2, §4).

#ifndef CC_ANALYZER_CONTEXT_ANALYZER_CONTEXT_H_
#define CC_ANALYZER_CONTEXT_ANALYZER_CONTEXT_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/checker_labels.h"
#include "util/result.h"

namespace cc::analyzer {

// An environment as "KEY=VALUE" strings; nullopt stands for the current
// environment of the program (Python: env=None).
using Environment = std::optional<std::vector<std::string>>;

// The context of the analyzer commands (Python: analyzer_context.Context).
// It is created once, by GetContext(), before other threads start; after
// that it is only read.
class AnalyzerContext {
 public:
  AnalyzerContext(const AnalyzerContext&) = delete;
  AnalyzerContext& operator=(const AnalyzerContext&) = delete;

  // The package root (Python: CC_DATA_FILES_DIR).
  const std::string& data_files_dir() const { return data_files_dir_; }
  const common::CheckerLabels& checker_labels() const {
    return checker_labels_;
  }
  // The package version, for example "6.30.0".
  const std::string& version() const { return version_; }
  // Python: package_git_tag and package_git_hash; nullopt for None.
  const std::optional<std::string>& git_tag() const { return git_tag_; }
  const std::optional<std::string>& git_hash() const { return git_hash_; }

  // Returns the binary of the analyzer `name`, or nullopt if it was not
  // found (Python: analyzer_binaries[name]).
  std::optional<std::string> AnalyzerBinary(std::string_view name) const;
  void SetAnalyzerBinary(std::string_view name, const std::string& binary);

  // Returns the environment to run `binary` in (Python: get_env_for_bin()):
  // the package environment for binaries of the package, else the original
  // environment saved by the launcher. A missing binary is logged and gets
  // the current environment.
  Environment EnvForBin(const std::string& binary) const;

  // Returns the folder of the Clang SA checker plugins, or nullopt (Python:
  // checker_plugin).
  std::optional<std::string> CheckerPluginDir() const;

 private:
  friend Result<AnalyzerContext*> GetContext();

  AnalyzerContext(std::string data_files_dir,
                  common::CheckerLabels checker_labels)
      : data_files_dir_(std::move(data_files_dir)),
        checker_labels_(std::move(checker_labels)) {}

  // Sets the analyzer binaries (Python: __populate_analyzers()).
  Result<void> PopulateAnalyzers(const std::vector<std::pair<std::string,
                                                             std::string>>&
                                     configured);

  std::string data_files_dir_;
  common::CheckerLabels checker_labels_;
  std::string version_;
  std::optional<std::string> git_tag_;
  std::optional<std::string> git_hash_;
  std::map<std::string, std::optional<std::string>, std::less<>>
      analyzer_binaries_;
  std::vector<std::string> path_env_extra_;
  std::vector<std::string> ld_lib_path_extra_;
  Environment original_env_;
  std::vector<std::string> package_env_;
};

// Returns the context, which is created on the first call. Problems are
// reported where they happen, as the Python version reports them, and give
// an error with exit code 1 and no message.
Result<AnalyzerContext*> GetContext();

// Returns the environment that the launcher saved in the file named by
// CODECHECKER_ORIGINAL_BUILD_ENV, or the current one if that variable is
// unset or empty. On a read error, the message is the text of Python's
// OSError.
Result<std::vector<std::string>> ReadOriginalEnvironment();

// Puts the folders `extra` in front of the variable `name` of `env`,
// separated by ':'; nothing if `extra` is empty (Python: env.extend()).
void PrependPaths(const std::string& name,
                  const std::vector<std::string>& extra,
                  std::vector<std::string>& env);

// The file name of the build logger library (Python:
// Context.logger_lib_name).
inline constexpr std::string_view kLoggerLibName = "ldlogger.so";

// Returns the folders where the package may keep ldlogger.so: under the
// package root, then under the library folder. Both come from environment
// variables of the launcher (CC_DATA_FILES_DIR, CC_LIB_DIR).
std::vector<std::string> LoggerLibRoots();

// Sets the variables of `env` that preload ldlogger.so and make it write
// `logfile` (Python: env.get_log_env()). With `use_absolute_ldpreload_path`,
// LD_PRELOAD gets the full path of the library that matches this program's
// architecture; otherwise it gets the library's name, and LD_LIBRARY_PATH
// gets every architecture folder.
void SetLogEnvironment(const std::string& logfile,
                       bool use_absolute_ldpreload_path,
                       std::vector<std::string>& env);

// Returns the environment to run `binary` in: EnvForBin() of the context,
// or nullopt (the current environment) if there is no context.
Environment EnvForBinary(const std::string& binary);

// Returns whether CC_ANALYZERS_FROM_PATH is "yes" or "1", in any case
// (Python: env.is_analyzer_from_path()).
bool IsAnalyzerFromPath();

// Returns the binary that best matches the regular expression
// `versioning_pattern` in the folders of PATH of `env` (Python:
// env.get_binary_in_path()): of several names, one of `basenames`, else the
// alphabetically last; of a name, the one earliest in PATH. nullopt if none.
std::optional<std::string> GetBinaryInPath(
    const std::vector<std::string>& basenames,
    std::string_view versioning_pattern, const Environment& env);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_CONTEXT_ANALYZER_CONTEXT_H_
