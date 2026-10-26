// Port of codechecker_analyzer/analyzers/clangtidy/: the Clang-Tidy driver
// (analyzer-drivers.md, analyzer-configuration.md).

#ifndef CC_ANALYZER_DRIVERS_CLANGTIDY_H_
#define CC_ANALYZER_DRIVERS_CLANGTIDY_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "util/memo.h"
#include "util/result.h"
#include "util/semver.h"

namespace cc::analyzer {

class ClangTidy final : public AnalyzerDriver {
 public:
  ClangTidy() = default;

  std::string_view name() const override { return "clang-tidy"; }
  std::optional<util::SemVer> Version() const override;
  std::optional<std::string> ResolveMissingBinary(
      const std::string& configured, const Environment& env) const override;
  std::vector<CheckerInfo> Checkers() const override;
  std::vector<AnalyzerOption> AnalyzerConfigOptions() const override;
  std::vector<CheckerOption> CheckerConfigOptions() const override;
  Result<AnalyzerConfig> ConstructConfig(const ConfigArgs& args) const override;
  std::vector<std::string> BuildCommand(AnalyzerRun* run) const override;
  std::set<std::string> MentionedFiles(AnalyzerRun* run,
                                       std::string_view output) const override;
  Result<void> PostprocessResult(
      AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
      const common::ReviewStatusHandler* review_status) const override;

 private:
  // Returns `clang-tidy -dump-config -checks=*`, or nullopt if it fails.
  std::optional<std::string> DumpConfig() const;

  mutable util::Memo<std::string, std::vector<CheckerInfo>> checkers_;
  mutable util::Memo<std::string, std::set<std::string>> group_prefixes_;
};

// Returns the Clang-Tidy driver.
const ClangTidy& ClangTidyAnalyzer();

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_CLANGTIDY_H_
