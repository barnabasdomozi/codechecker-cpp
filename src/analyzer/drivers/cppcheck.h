// Port of codechecker_analyzer/analyzers/cppcheck/: the Cppcheck driver
// (analyzer-drivers.md).

#ifndef CC_ANALYZER_DRIVERS_CPPCHECK_H_
#define CC_ANALYZER_DRIVERS_CPPCHECK_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analyzer/drivers/analyzer.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "util/memo.h"
#include "util/result.h"
#include "util/semver.h"

namespace cc::analyzer {

class Cppcheck final : public AnalyzerDriver {
 public:
  Cppcheck() = default;

  std::string_view name() const override { return "cppcheck"; }
  std::optional<util::SemVer> Version() const override;
  std::optional<std::string> IncompatibleVersion() const override;
  std::optional<std::string> ResolveMissingBinary(
      const std::string& configured, const Environment& env) const override;
  std::vector<CheckerInfo> Checkers() const override;
  std::vector<AnalyzerOption> AnalyzerConfigOptions() const override;
  Result<AnalyzerConfig> ConstructConfig(const ConfigArgs& args) const override;
  std::vector<std::string> BuildCommand(AnalyzerRun* run) const override;
  void PostAnalyze(AnalyzerRun* run) const override;
  Result<void> PostprocessResult(
      AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
      const common::ReviewStatusHandler* review_status) const override;

 private:
  mutable util::Memo<std::string, std::vector<CheckerInfo>> checkers_;
};

// Returns the Cppcheck driver.
const Cppcheck& CppcheckAnalyzer();

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_CPPCHECK_H_
