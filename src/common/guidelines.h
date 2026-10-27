// Port of codechecker_common/guidelines.py: the rules of the coding
// guidelines (SEI CERT, CWE, ...) in config/guidelines, which checker labels
// refer to as "<guideline>:<rule>" (checker-labels.md).

#ifndef CC_COMMON_GUIDELINES_H_
#define CC_COMMON_GUIDELINES_H_

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::common {

// The rules of every guideline. Read-only after Load(), so it can be used
// from several threads.
class Guidelines {
 public:
  // A rule of a guideline. The fields other than the id are "" if the file
  // does not give them.
  struct Rule {
    std::string id;
    std::string title;
    std::string url;  // rule_url
    std::string level;
  };

  // Reads the guideline files in the folder `guidelines_dir`, every file but
  // the *.license ones. A file in the wrong format is logged and skipped;
  // of two files of the same guideline, the one listed later counts. Fails
  // with Python's message if `guidelines_dir` is not a folder.
  static Result<Guidelines> Load(const std::string& guidelines_dir);

  Guidelines(Guidelines&& other) = default;
  Guidelines& operator=(Guidelines&& other) = default;

  // Returns the rules of the guideline `name`, in the order of its file;
  // empty if there is no such guideline (Python: rules_of_guideline()).
  const std::vector<Rule>& RulesOfGuideline(std::string_view name) const;

 private:
  Guidelines() = default;

  std::map<std::string, std::vector<Rule>, std::less<>> rules_;
};

}  // namespace cc::common

#endif  // CC_COMMON_GUIDELINES_H_
