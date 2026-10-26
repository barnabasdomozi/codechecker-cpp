// Port of codechecker_analyzer/buildlog/log_parser.py: compilation
// database entries turned into build actions, with the implicit compiler
// information, skipping and uniqueing (compiler-info.md).

#ifndef CC_ANALYZER_BUILDLOG_LOG_PARSER_H_
#define CC_ANALYZER_BUILDLOG_LOG_PARSER_H_

#include <yyjson.h>

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "common/skiplist_handler.h"
#include "util/pyre.h"
#include "util/result.h"

namespace cc::analyzer {

// The implicit settings of a compiler (Python: ImplicitCompilerInfo).
struct CompilerInfo {
  std::vector<std::string> compiler_includes;
  std::string compiler_standard;
  std::string target;
};

// What the implicit settings depend on: the compiler, the language and the
// flags that change them (Python: ImplicitInfoSpecifierKey).
struct CompilerInfoKey {
  std::string compiler;
  std::string language;
  std::vector<std::string> flags;

  friend bool operator==(const CompilerInfoKey& a,
                         const CompilerInfoKey& b) = default;
};

// The implicit settings found so far, shared by the parallel parsing.
class ImplicitCompilerInfo {
 public:
  ImplicitCompilerInfo() = default;
  ImplicitCompilerInfo(const ImplicitCompilerInfo&) = delete;
  ImplicitCompilerInfo& operator=(const ImplicitCompilerInfo&) = delete;

  // Returns the settings of `key`, running the compiler the first time.
  CompilerInfo Get(const CompilerInfoKey& key);

  // Returns the settings as compiler_info.json holds them (Python:
  // dump_compiler_info()).
  std::string Dump() const;

 private:
  mutable std::mutex mutex_;
  std::vector<std::pair<CompilerInfoKey, CompilerInfo>> entries_;
};

// Returns the settings of a compiler info file (Python:
// load_compiler_info()); none if the file cannot be read.
std::vector<std::pair<CompilerInfoKey, CompilerInfo>> LoadCompilerInfo(
    const std::string& path);

// The settings of ParseUniqueLog() (Python: the parameters of
// parse_unique_log()).
struct LogParseOptions {
  std::string compile_uniqueing = "none";
  std::optional<std::string> compiler_info_file;
  bool keep_gcc_include_fixed = false;
  bool keep_gcc_intrin = false;
  int jobs = 1;
  const common::SkipListHandlers* analysis_skip_handlers = nullptr;
  const common::SkipListHandlers* pre_analysis_skip_handlers = nullptr;
  bool ctu_or_stats_enabled = false;
};

// The build actions of a compilation database.
struct ParsedLog {
  std::vector<BuildAction> actions;
  int skipped = 0;  // Entries that failed, were skipped or not compiled.
};

// Returns the build actions of the compilation database
// `compilation_database`, an array of entries (Python: parse_unique_log()).
// Response files in the commands are expanded in the entries. Errors are
// logged; the error has exit code 1.
Result<ParsedLog> ParseUniqueLog(yyjson_mut_doc* document,
                                 yyjson_mut_val* compilation_database,
                                 const LogParseOptions& options,
                                 ImplicitCompilerInfo* compiler_info);

// The compiler flags that Clang does not take from GCC commands (Python:
// IGNORED_OPTIONS_GCC); Infer leaves them out too.
const util::Regex& IgnoredOptionsGcc();

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_BUILDLOG_LOG_PARSER_H_
