// Port of codechecker_analyzer/analyzers/gcc/: the GCC Static Analyzer
// driver (analyzer-drivers.md).

#ifndef CC_ANALYZER_DRIVERS_GCC_H_
#define CC_ANALYZER_DRIVERS_GCC_H_

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

class Gcc final : public AnalyzerDriver {
 public:
  Gcc() = default;

  std::string_view name() const override { return "gcc"; }
  std::optional<util::SemVer> Version() const override;
  std::optional<std::string> IncompatibleVersion() const override;
  std::vector<CheckerInfo> Checkers() const override;
  std::vector<AnalyzerOption> AnalyzerConfigOptions() const override;
  Result<AnalyzerConfig> ConstructConfig(const ConfigArgs& args) const override;
  std::vector<std::string> BuildCommand(AnalyzerRun* run) const override;
  Result<void> PostprocessResult(
      AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
      const common::ReviewStatusHandler* review_status) const override;

 private:
  mutable util::Memo<std::string, std::vector<CheckerInfo>> checkers_;
};

// Returns the GCC driver.
const Gcc& GccAnalyzer();

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_GCC_H_
