// Port of codechecker_common/skiplist_handler.py: skip files, which say
// which source files to leave out (skip-and-file-filters.md §1).

#ifndef CC_COMMON_SKIPLIST_HANDLER_H_
#define CC_COMMON_SKIPLIST_HANDLER_H_

#include <string_view>
#include <vector>

#include "util/pyre.h"

namespace cc::common {

// The rules of one skip file. Lines "-<glob>" skip and "+<glob>" keep the
// paths that match; the first matching rule decides.
class SkipListHandler {
 public:
  // Parses the skip file `content`. Malformed lines are logged and left out.
  explicit SkipListHandler(std::string_view content);

  SkipListHandler(SkipListHandler&& other) = default;
  SkipListHandler& operator=(SkipListHandler&& other) = default;

  // Returns whether `path` is skipped.
  bool ShouldSkip(std::string_view path) const;

 private:
  struct Rule {
    bool skip;  // "-" lines skip, "+" lines keep.
    util::Regex regex;
  };

  std::vector<Rule> rules_;
};

// Several skip files; a path is skipped if any of them skips it (Python:
// SkipListHandlers).
using SkipListHandlers = std::vector<SkipListHandler>;

// Returns whether any of `handlers` skips `path`.
bool ShouldSkip(const SkipListHandlers& handlers, std::string_view path);

}  // namespace cc::common

#endif  // CC_COMMON_SKIPLIST_HANDLER_H_
