// Port of codechecker_analyzer/analyzers/clangsa/: the Clang Static
// Analyzer driver (analyzer-drivers.md, ctu-analysis.md).

#ifndef CC_ANALYZER_DRIVERS_CLANGSA_H_
#define CC_ANALYZER_DRIVERS_CLANGSA_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "util/memo.h"
#include "util/result.h"
#include "util/semver.h"

namespace cc::analyzer {

// The tool that maps the functions of a translation unit for CTU, and the
// name of its merged output file.
struct CtuMappingTool {
  std::string tool;
  std::string map_file;
};

class ClangSa final : public AnalyzerDriver {
 public:
  ClangSa() = default;

  std::string_view name() const override { return "clangsa"; }
  std::optional<util::SemVer> Version() const override;
  std::optional<std::string> ResolveMissingBinary(
      const std::string& configured, const Environment& env) const override;
  // The checkers, alpha ones included.
  std::vector<CheckerInfo> Checkers() const override;
  std::vector<AnalyzerOption> AnalyzerConfigOptions() const override;
  std::vector<CheckerOption> CheckerConfigOptions() const override;
  Result<AnalyzerConfig> ConstructConfig(const ConfigArgs& args) const override;
  std::vector<std::string> BuildCommand(AnalyzerRun* run) const override;
  void PostAnalyze(AnalyzerRun* run) const override;
  std::set<std::string> MentionedFiles(AnalyzerRun* run,
                                       std::string_view output) const override;
  Result<void> PostprocessResult(
      AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
      const common::ReviewStatusHandler* review_status) const override;

  // Returns the checkers, with the alpha and the developer ones if asked
  // (Python: get_analyzer_checkers(alpha, debug)).
  std::vector<CheckerInfo> CheckersWith(bool alpha, bool debug) const;

  // Returns the .so files of the checker plugins (Python:
  // analyzer_plugins()).
  std::vector<std::string> Plugins() const;

  // Returns the CTU mapping tool next to the Clang binary, or nullopt
  // (Python: ctu_mapping()).
  std::optional<CtuMappingTool> CtuMapping() const;

  // Returns whether the CTU mapping tool runs (Python: is_ctu_capable()).
  bool IsCtuCapable() const;

  // Returns whether Clang can parse CTU translation units on demand
  // (Python: is_on_demand_ctu_available()).
  bool IsOnDemandCtuAvailable() const;

  // Returns the CTU folder of the architecture of `run`'s action (Python:
  // get_ctu_dir()).
  std::string CtuDir(AnalyzerRun* run) const;

 private:
  // Returns `clang -cc1 --help`, or "" if it fails.
  std::string Cc1Help() const;

  // Returns the flags that load the plugins into `clang -cc1`.
  std::vector<std::string> PluginLoadFlags() const;

  // Returns the flags that turn on CTU progress messages, if Clang has them
  // (Python: display_progress()).
  std::vector<std::string> DisplayProgressFlags() const;

  mutable util::Memo<std::string, std::string> cc1_help_;
  mutable util::Memo<std::tuple<std::string, bool, bool>,
                     std::vector<CheckerInfo>>
      checkers_;
  mutable util::Memo<std::string, bool> display_progress_;
};

// Returns the Clang SA driver.
const ClangSa& ClangSaAnalyzer();

// Returns the (flag, description) entries of a Clang help page after the
// line `start_label`, from the output of `command` (Python:
// parse_clang_help_page()).
std::vector<std::pair<std::string, std::string>> ParseClangHelpPage(
    const std::vector<std::string>& command, std::string_view start_label);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_CLANGSA_H_
