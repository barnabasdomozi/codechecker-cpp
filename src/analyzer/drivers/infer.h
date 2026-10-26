// Port of codechecker_analyzer/analyzers/infer/: the Facebook Infer driver
// (analyzer-drivers.md).

#ifndef CC_ANALYZER_DRIVERS_INFER_H_
#define CC_ANALYZER_DRIVERS_INFER_H_

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

class Infer final : public AnalyzerDriver {
 public:
  Infer() = default;

  std::string_view name() const override { return "infer"; }
  std::optional<util::SemVer> Version() const override;
  std::vector<CheckerInfo> Checkers() const override;
  std::vector<AnalyzerOption> AnalyzerConfigOptions() const override;
  Result<AnalyzerConfig> ConstructConfig(const ConfigArgs& args) const override;
  std::vector<std::string> BuildCommand(AnalyzerRun* run) const override;
  // The environment for the binary, with TZ=UTC.
  Environment CommandEnvironment(
      const std::vector<std::string>& command) const override;
  void PostAnalyze(AnalyzerRun* run) const override;
  Result<void> PostprocessResult(
      AnalyzerRun* run, const common::SkipListHandlers* skip_handlers,
      const common::ReviewStatusHandler* review_status) const override;

 private:
  mutable util::Memo<std::string, std::optional<util::SemVer>> versions_;
  mutable util::Memo<std::string, std::vector<CheckerInfo>> checkers_;
};

// Returns the Infer driver.
const Infer& InferAnalyzer();

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_INFER_H_
