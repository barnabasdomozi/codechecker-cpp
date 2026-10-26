// Port of the review status rules file part of
// codechecker_common/review_status_handler.py (review_status.yaml;
// review-status-and-suppression.md §4).

#ifndef CC_COMMON_REVIEW_STATUS_HANDLER_H_
#define CC_COMMON_REVIEW_STATUS_HANDLER_H_

#include <optional>
#include <string>

#include "report/model/report.h"
#include "util/json.h"
#include "util/result.h"

namespace cc::common {

// The rules of a review status configuration file.
class ReviewStatusHandler {
 public:
  ReviewStatusHandler() = default;
  ReviewStatusHandler(ReviewStatusHandler&& other) = default;
  ReviewStatusHandler& operator=(ReviewStatusHandler&& other) = default;

  // Reads and checks the rules file at `path` (Python:
  // set_review_status_config()). Fails with the message of Python's
  // ValueError.
  Result<void> SetReviewStatusConfig(const std::string& path);

  // Returns whether a rule with the "ignore" action matches `report`, so
  // the report is left out of the results (Python: should_ignore()).
  bool ShouldIgnore(const report::Report& report) const;

 private:
  std::optional<util::JsonDocument> data_;
  std::string path_;
};

}  // namespace cc::common

#endif  // CC_COMMON_REVIEW_STATUS_HANDLER_H_
