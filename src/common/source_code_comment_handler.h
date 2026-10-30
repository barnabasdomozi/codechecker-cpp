// Port of codechecker_common/source_code_comment_handler.py: review status
// comments in the source code, such as
// "// codechecker_suppress [core.DivideZero] Not a bug."
// (review-status-and-suppression.md §3).

#ifndef CC_COMMON_SOURCE_CODE_COMMENT_HANDLER_H_
#define CC_COMMON_SOURCE_CODE_COMMENT_HANDLER_H_

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::common {

// The values of the --review-status filters (Python: REVIEW_STATUS_VALUES).
inline constexpr std::string_view kReviewStatusValues[] = {
    "confirmed", "false_positive", "intentional", "suppress", "unreviewed"};

// A review status comment in the source code.
struct SourceCodeComment {
  // The checker names it applies to, without repetitions; "all" means
  // every checker.
  std::vector<std::string> checkers;
  std::string message;
  std::string status;  // false_positive, intentional or confirmed.
  // The comment as it is written: its lines, with their line ends, joined
  // by spaces.
  std::string line;
};

// Returns the comments in the source file of `lines` (see util::SplitLines())
// that belong to a report of `checker_name` on line `bug_line` (Python:
// filter_source_line_comments()): those above the line that name a checker
// contained in `checker_name`, or "all". A comment is returned once for each
// such name. A comment that contains a marker but cannot be parsed fails with
// the message "misspelled review status comment @<line>: <comment>".
Result<std::vector<SourceCodeComment>> FilterSourceLineComments(
    const std::vector<std::string_view>& lines, int bug_line,
    std::string_view checker_name);

// The result of ScanSourceLineComments().
struct SourceCommentScan {
  // The number of lines whose comments could be read; they may have none.
  int scanned_lines = 0;
  // The errors of the lines whose comments are misspelled, as
  // FilterSourceLineComments() words them.
  std::vector<std::string> misspelled;
};

// Reads the comments above each of `lines` in the source `text` (Python:
// scan_source_line_comments()). Nothing is read if the text does not contain
// "codechecker_".
SourceCommentScan ScanSourceLineComments(std::string_view text,
                                         const std::set<int>& lines);

}  // namespace cc::common

#endif  // CC_COMMON_SOURCE_CODE_COMMENT_HANDLER_H_
