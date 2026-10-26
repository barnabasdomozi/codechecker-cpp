// Port of codechecker_common/review_status_handler.py: the review status of
// a report from its source code comment or from the review_status.yaml
// rules file (review-status-and-suppression.md §2-§4).

#ifndef CC_COMMON_REVIEW_STATUS_HANDLER_H_
#define CC_COMMON_REVIEW_STATUS_HANDLER_H_

#include <optional>
#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/json.h"
#include "util/result.h"

namespace cc::common {

// Finds the review status of reports, and keeps the rules of a review
// status configuration file. The const methods can be called from several
// threads at the same time; the others need exclusive access.
class ReviewStatusHandler {
 public:
  ReviewStatusHandler() = default;
  ReviewStatusHandler(ReviewStatusHandler&& other) = default;
  ReviewStatusHandler& operator=(ReviewStatusHandler&& other) = default;

  // Reads and checks the rules file at `path`, which replaces the rules
  // read before (Python: set_review_status_config()). Fails with the
  // message of Python's ValueError.
  Result<void> SetReviewStatusConfig(const std::string& path);

  // Returns whether a rule with the "ignore" action matches `report`, so
  // the report is left out of the results (Python: should_ignore()).
  bool ShouldIgnore(const report::Report& report) const;

  // Returns the review status of `report` (Python: get_review_status()):
  // that of its source code comment if its main file has not changed since
  // the analysis, else that of the first matching rule, else unreviewed.
  // More than one matching source code comment fails with the message of
  // Python's ValueError. Misspelled comments are added to the warnings.
  Result<report::ReviewStatus> GetReviewStatus(const report::Report& report);

  // Returns the warnings about misspelled source code comments, in the
  // order they were found (Python: source_comment_warnings()).
  const std::vector<std::string>& source_comment_warnings() const {
    return source_comment_warnings_;
  }

 private:
  // Python: get_review_status_from_source(); nullopt if no source code
  // comment applies.
  Result<std::optional<report::ReviewStatus>> ReviewStatusFromSource(
      const report::Report& report);

  // Python: get_review_status_from_config(); nullopt if no rule applies.
  std::optional<report::ReviewStatus> ReviewStatusFromConfig(
      const report::Report& report) const;

  std::optional<util::JsonDocument> data_;
  std::string path_;
  std::vector<std::string> source_comment_warnings_;
};

}  // namespace cc::common

#endif  // CC_COMMON_REVIEW_STATUS_HANDLER_H_
