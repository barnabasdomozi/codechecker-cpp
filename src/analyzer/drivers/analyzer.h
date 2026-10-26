// The analyzer drivers (architecture.md §8.1): one class per analyzer
// behind a common interface, and the data they share. Port of
// codechecker_analyzer/analyzers/analyzer_base.py, config_handler.py and
// result_handler_base.py (analyzer-drivers.md, checker-selection.md).

#ifndef CC_ANALYZER_DRIVERS_ANALYZER_H_
#define CC_ANALYZER_DRIVERS_ANALYZER_H_

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "analyzer/context/analyzer_context.h"
#include "common/checker_labels.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "util/logging.h"
#include "util/memo.h"
#include "util/result.h"
#include "util/semver.h"

namespace cc::analyzer {

// An --analyzer-config item: analyzer:option=value.
struct AnalyzerConfigArg {
  std::string analyzer;
  std::string option;
  std::string value;
};

// A --checker-config item: analyzer:checker:option=value.
struct CheckerConfigArg {
  std::string analyzer;
  std::string checker;
  std::string option;
  std::string value;
};

// A -e or -d item: a checker, prefix, profile or label, and whether it is
// enabled (-e) or disabled (-d).
struct CheckerChoice {
  std::string name;
  bool enabled = false;
};

// The options of a command that decide how the analyzers are configured
// (Python: the attributes of `args` that construct_config_handler() reads).
struct ConfigArgs {
  std::optional<std::string> report_hash;
  bool enable_z3_refutation = false;
  bool add_gcc_include_dirs_with_isystem = false;
  // The CTU folder if CTU analysis is on, else empty.
  std::string ctu_dir;
  bool ctu_on_demand = false;
  // The -e/-d items, in command line order.
  std::vector<CheckerChoice> ordered_checkers;
  bool enable_all = false;
  bool stats_enabled = false;
  std::vector<AnalyzerConfigArg> analyzer_config;
  std::optional<std::vector<CheckerConfigArg>> checker_config;
};

enum class CheckerState { kDisabled, kEnabled };

// A checker of an analyzer and its state.
struct CheckerEntry {
  std::string name;
  CheckerState state = CheckerState::kDisabled;
  std::string description;
};

// The checkers of an analyzer with their states (Python: the ordered
// dictionary of AnalyzerConfigHandler). A checker whose state is set moves
// to the end, so the order says which decision came last.
class CheckerTable {
 public:
  CheckerTable() = default;
  CheckerTable(CheckerTable&& other) = default;
  CheckerTable& operator=(CheckerTable&& other) = default;

  // Adds a checker, or replaces the one with the same name.
  void Add(const std::string& name, const std::string& description);

  // Sets the state of the checker `name`, or with `strict` false, of every
  // checker whose name starts with `name` at a word boundary (Python:
  // set_checker_enabled()).
  void SetEnabled(std::string_view name, bool enabled, bool strict);

  const std::list<CheckerEntry>& entries() const { return entries_; }

  // Returns the checker `name`, or null.
  const CheckerEntry* Find(std::string_view name) const;

  // Returns whether a checker is enabled.
  bool HasEnabled() const;

 private:
  std::list<CheckerEntry> entries_;
  std::unordered_map<std::string, std::list<CheckerEntry>::iterator> index_;
};

// Returns whether `checker` starts with `name` followed by a regular
// expression word boundary (Python: re.match("^" + re.escape(name) +
// "\\b.*$", checker)).
bool MatchesCheckerPrefix(std::string_view name, std::string_view checker);

// The configuration of one analyzer for an analysis (Python: the
// AnalyzerConfigHandler subclasses). Only the fields of its analyzer are
// used.
struct AnalyzerConfig {
  std::string analyzer;
  CheckerTable checkers;
  // The arguments of cc-verbatim-args-file.
  std::vector<std::string> analyzer_extra_arguments;
  std::optional<std::string> report_hash;
  bool enable_all = false;
  bool add_gcc_include_dirs_with_isystem = false;
  // Clang SA: "checker:option=value" and "option=value" items.
  std::vector<std::string> checker_config_options;
  std::string ctu_dir;
  bool ctu_on_demand = false;
  bool enable_z3_refutation = false;
  // Clang-Tidy: the -config JSON (empty: none), and what it decides.
  std::string tidy_config_json;
  bool take_config_from_directory = false;
  bool checks_in_config = false;
  // Cppcheck and Infer: the values of each --analyzer-config option.
  std::map<std::string, std::vector<std::string>> option_values;
};

// One run of an analyzer on a build action (Python: a SourceAnalyzer object
// with its ResultHandler).
struct AnalyzerRun {
  const BuildAction* action = nullptr;
  const AnalyzerConfig* config = nullptr;
  std::string workspace;  // The output folder.
  std::string source_file;
  // Python: buildaction_hash and analyzer_action_str.
  std::string action_hash;
  std::string action_str;
  std::string result_file;  // <workspace>/<action_str>.plist
  std::string fixit_file;   // <workspace>/fixit/<action_str>.yaml
  // Clang SA: extra -analyzer-config arguments (statistics), whether CTU is
  // turned off for this run, and the disabled checkers.
  std::vector<std::vector<std::string>> checker_configs;
  bool disable_ctu = false;
  std::vector<std::string> disabled_checkers;
  // Clang SA: the CTU folder of the action's architecture, once computed.
  std::optional<std::string> ctu_dir;
  // The command and what the analyzer did.
  std::vector<std::string> analyzer_cmd;
  std::string out;
  std::string err;
  int return_code = 1;
};

// Returns the hash that names the result files of the analysis of `source`
// in `directory` with the build command `command` (Python:
// util.analyzer_action_hash()). The -o arguments are left out, because
// make 4.3 logs commands with random output names. Fails if `command`
// cannot be split.
Result<std::string> AnalyzerActionHash(const std::string& source,
                                       const std::string& directory,
                                       const std::string& command);

// Returns the run of the analyzer of `config` on `action`, with its result
// file names, writing into `workspace`. Fails if the command of the action
// cannot be split.
Result<AnalyzerRun> MakeAnalyzerRun(const BuildAction& action,
                                    const AnalyzerConfig& config,
                                    const std::string& workspace);

// Returns <workspace>/<folder>/<action hash>, the output folder of the
// analyzers that write into a folder (Python: Path(workspace, folder,
// buildaction_hash), so no trailing slash while the hash is empty).
std::string ActionOutputDir(const AnalyzerRun& run, std::string_view folder);

// A checker of an analyzer and its description.
struct CheckerInfo {
  std::string name;
  std::string description;
};

// An option of --analyzer-config.
struct AnalyzerOption {
  std::string option;
  std::string documentation;
  bool must_exist = false;  // The value is a path that must exist.
};

// The --analyzer-config option, known to every analyzer, that names a file
// of arguments forwarded verbatim to the analyzer.
inline constexpr std::string_view kVerbatimArgsOption = "cc-verbatim-args-file";

// Returns the description of kVerbatimArgsOption.
AnalyzerOption VerbatimArgsOption();

// Returns the arguments in the kVerbatimArgsOption files of `analyzer` in
// `items` (Python: load_args_from_file()); of several files the last one
// counts, and none gives no arguments. A file that cannot be read is logged
// to `log` and is an error with exit code 1.
Result<std::vector<std::string>> LoadVerbatimArgs(
    std::string_view analyzer, const std::vector<AnalyzerConfigArg>& items,
    const util::Logger& log);

// An option of --checker-config.
struct CheckerOption {
  std::string checker;
  std::string option;
  std::string documentation;
};

// One analyzer: how to query it, how to configure it, how to build its
// command and how to turn its output into a result file. The facts queried
// from the binary are cached; the objects can be used from several threads.
class AnalyzerDriver {
 public:
  AnalyzerDriver(const AnalyzerDriver&) = delete;
  AnalyzerDriver& operator=(const AnalyzerDriver&) = delete;
  virtual ~AnalyzerDriver() = default;

  // Returns the analyzer's name, for example "clangsa".
  virtual std::string_view name() const = 0;

  // Returns the binary of the analyzer, or nullopt.
  std::optional<std::string> Binary() const;

  // Returns the version of the binary, even if it is not supported; nullopt
  // if it cannot be found out (Python: get_binary_version()).
  virtual std::optional<util::SemVer> Version() const = 0;

  // Returns why the version of the binary is not supported, or nullopt
  // (Python: is_binary_version_incompatible()).
  virtual std::optional<std::string> IncompatibleVersion() const;

  // Returns a binary in PATH to use instead of the configured one, which
  // has no folder (Python: resolve_missing_binary()).
  virtual std::optional<std::string> ResolveMissingBinary(
      const std::string& configured, const Environment& env) const;

  // Returns the checkers of the analyzer (Python: get_analyzer_checkers()).
  virtual std::vector<CheckerInfo> Checkers() const = 0;

  // Returns the --analyzer-config options (Python: get_analyzer_config()).
  virtual std::vector<AnalyzerOption> AnalyzerConfigOptions() const = 0;

  // Returns the --checker-config options (Python: get_checker_config()).
  virtual std::vector<CheckerOption> CheckerConfigOptions() const;

  // Returns the configuration of an analysis (Python:
  // construct_config_handler()). Problems are logged; the error has exit
  // code 1.
  virtual Result<AnalyzerConfig> ConstructConfig(
      const ConfigArgs& args) const = 0;

  // Returns the analyzer command for `run` (Python:
  // construct_analyzer_cmd()); empty if it cannot be built.
  virtual std::vector<std::string> BuildCommand(AnalyzerRun* run) const = 0;

  // Returns the environment of the analyzer command (Python: the env that
  // analyze() passes to run_proc()).
  virtual Environment CommandEnvironment(
      const std::vector<std::string>& command) const;

  // Runs right after the analyzer (Python: post_analyze()).
  virtual void PostAnalyze(AnalyzerRun* run) const;

  // Returns the files that the analyzer output `output` mentions (Python:
  // get_analyzer_mentioned_files()).
  virtual std::set<std::string> MentionedFiles(AnalyzerRun* run,
                                               std::string_view output) const;

  // Turns the analyzer's output into the result file and the error side
  // file (Python: ResultHandler.postprocess_result()). Reports of files
  // that `skip_handlers` skip are dropped, and so are those the rules of
  // `review_status` ignore.
  virtual Result<void> PostprocessResult(
      AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
      const common::ReviewStatusHandler* review_status) const = 0;

 protected:
  AnalyzerDriver() = default;

  // Python: AnalyzerConfigHandler.initialize_checkers(). Adds `checkers`
  // to `config` and sets their states from the default profile,
  // --enable-all and the -e/-d items.
  static Result<void> InitializeCheckers(
      const std::vector<CheckerInfo>& checkers, const ConfigArgs& args,
      AnalyzerConfig* config);

  // Returns the version of Binary(), found out once per binary (Python:
  // get_binary_version()): `parse` reads it from the output of the binary
  // run with `flag`. A run that fails is logged to `log` and gives nullopt.
  std::optional<util::SemVer> BinaryVersion(
      std::string_view flag,
      const std::function<std::optional<util::SemVer>(
          const std::string& output)>& parse,
      const util::Logger& log) const;

 private:
  mutable util::Memo<std::string, std::optional<util::SemVer>>
      binary_versions_;
};

// Returns the analyzers in registry order: clangsa, clang-tidy, cppcheck,
// gcc, infer (Python: analyzer_types.supported_analyzers).
std::span<const AnalyzerDriver* const> SupportedAnalyzers();

// Returns the analyzer `name`, or null.
const AnalyzerDriver* FindAnalyzer(std::string_view name);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_ANALYZER_H_
