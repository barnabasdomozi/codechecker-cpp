// Port of codechecker_analyzer/analyzers/flag.py: helpers for building
// analyzer command lines.

#ifndef CC_ANALYZER_DRIVERS_FLAG_H_
#define CC_ANALYZER_DRIVERS_FLAG_H_

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cc::analyzer {

// Returns whether an argument of `command` starts with `flag` (Python:
// has_flag()).
inline bool HasFlag(std::string_view flag,
                    const std::vector<std::string>& command) {
  return std::ranges::any_of(command, [flag](const std::string& arg) {
    return arg.starts_with(flag);
  });
}

// Appends `flag` and each of `params` to `command`: -I a -I b (Python:
// prepend_all()).
inline void AppendPrependedAll(const std::string& flag,
                               const std::vector<std::string>& params,
                               std::vector<std::string>* command) {
  for (const std::string& param : params) {
    command->push_back(flag);
    command->push_back(param);
  }
}

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_FLAG_H_
