// Port of codechecker_common/checker_labels.py: the labels of the checkers
// (severity, profile, guideline, ...) from config/labels
// (checker-labels.md).

#ifndef CC_COMMON_CHECKER_LABELS_H_
#define CC_COMMON_CHECKER_LABELS_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "util/result.h"

namespace cc::common {

// A label: its key and value, for example ("severity", "HIGH").
using Label = std::pair<std::string, std::string>;

// Returns the key and the value of "key:value", both stripped (Python:
// split_label_kv()). Without a colon, the value is empty.
Label SplitLabel(std::string_view key_value);

// The labels of every checker of every analyzer. Read-only after Load(), so
// it can be used from several threads.
class CheckerLabels {
 public:
  // Reads `labels_dir`/analyzers/*.json and `labels_dir`/descriptions.json.
  // Fails with the message of Python's exception for a missing folder or a
  // label file in the wrong format.
  static Result<CheckerLabels> Load(const std::string& labels_dir);

  CheckerLabels(CheckerLabels&& other) = default;
  CheckerLabels& operator=(CheckerLabels&& other) = default;

  // Returns the checkers that have at least one of the "key:value" labels
  // `filter_labels`, of `analyzer` or of all analyzers (Python:
  // checkers_by_labels()).
  std::vector<std::string> CheckersByLabels(
      const std::vector<std::string>& filter_labels,
      std::optional<std::string_view> analyzer = std::nullopt) const;

  // Returns the checker names of `analyzer`, or of all analyzers.
  std::vector<std::string> Checkers(
      std::optional<std::string_view> analyzer = std::nullopt) const;

  // Returns the label keys that occur, in the order they first occur
  // (Python: labels(), a set).
  const std::vector<std::string>& Labels() const { return label_keys_; }

  // Returns the values of the label `key` that occur, in the order they
  // first occur (Python: occurring_values(), a set).
  const std::vector<std::string>& OccurringValues(std::string_view key) const;

  // Returns the value of the unique label `key` (severity, blacklist,
  // description) of `checker`, or the label's default (Python:
  // label_of_checker()). A checker without labels of its own gets those of
  // the first checker name in the file that is its prefix.
  std::string UniqueLabelOfChecker(const std::string& checker,
                                   std::string_view key,
                                   std::optional<std::string_view> analyzer)
      const;

  // Returns the values of the label `key` of `checker`, of `analyzer` or of
  // all analyzers, without repetitions, in the order of the label files
  // (Python: label_of_checker() for labels that are not unique). Checkers
  // without labels of their own are found as by UniqueLabelOfChecker().
  std::vector<std::string> LabelsOfChecker(
      const std::string& checker, std::string_view key,
      std::optional<std::string_view> analyzer) const;

  // Returns the values of the label `key` with their descriptions from
  // descriptions.json, in the file's order (Python: get_description()).
  const std::vector<std::pair<std::string, std::string>>& Description(
      std::string_view key) const;

 private:
  struct Checker {
    std::string name;
    std::vector<Label> labels;
  };
  struct Analyzer {
    std::string name;
    std::vector<Checker> checkers;  // In the order of the label file.
    std::unordered_map<std::string, size_t> index;  // Name -> checker.
  };

  CheckerLabels() = default;

  // Returns the checker of `data` that has the labels of `checker`: the one
  // with that name, else the first whose name is a prefix of it; null if
  // there is none.
  static const Checker* FindChecker(const Analyzer& data,
                                    const std::string& checker);

  std::vector<Analyzer> analyzers_;
  std::vector<std::string> label_keys_;
  std::map<std::string, std::vector<std::string>, std::less<>> values_;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>,
           std::less<>>
      descriptions_;
};

}  // namespace cc::common

#endif  // CC_COMMON_CHECKER_LABELS_H_
