#include "common/checker_labels.h"

#include <yyjson.h>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/package.h"
#include "common/util.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/json.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::common {
namespace {

// The labels that a checker has at most once, and their defaults.
constexpr std::pair<std::string_view, std::string_view> kUniqueLabels[] = {
    {"severity", "UNSPECIFIED"}, {"blacklist", "false"}, {"description", ""}};

// Returns the labels of one checker, or the ValueError of
// __check_json_format() if they are in the wrong format.
Result<std::vector<Label>> CheckerLabelList(const std::string& checker,
                                            yyjson_val* labels) {
  using Labels = Result<std::vector<Label>>;
  bool strings = yyjson_is_arr(labels);
  yyjson_arr_iter iter = yyjson_arr_iter_with(labels);
  while (yyjson_val* label = yyjson_arr_iter_next(&iter)) {
    strings = strings && yyjson_is_str(label);
  }
  if (!strings) {
    return Labels(
        Error{.message = std::format("\"{}\" should be assigned a string list.",
                                     checker)});
  }
  std::vector<Label> result;
  iter = yyjson_arr_iter_with(labels);
  while (yyjson_val* label = yyjson_arr_iter_next(&iter)) {
    const std::string text = util::JsonString(label);
    if (text.find(':') == std::string::npos) {
      return Labels(Error{
          .message = std::format("All labels at \"{}\" should be in the "
                                 "following format: <label>:<property>.",
                                 checker)});
    }
    result.push_back(SplitLabel(text));
  }
  for (const auto& [unique, default_value] : kUniqueLabels) {
    if (std::ranges::count(result, unique, &Label::first) > 1) {
      return Labels(Error{.message = std::format(
                              "Label \"severity\" should be unique for checker "
                              "{}.",
                              checker)});
    }
  }
  return Labels(std::move(result));
}

}  // namespace

std::string LabelsDirectory() {
  if (const std::optional<std::string> test_dir =
          platform::GetEnv("CC_TEST_LABELS_DIR")) {
    return *test_dir;
  }
  return util::pypath::Join({DataFilesDir(), "config", "labels"});
}

Label SplitLabel(std::string_view key_value) {
  const size_t colon = key_value.find(':');
  if (colon == std::string_view::npos) {
    return {std::string(util::Strip(key_value)), ""};
  }
  return {std::string(util::Strip(key_value.substr(0, colon))),
          std::string(util::Strip(key_value.substr(colon + 1)))};
}

Result<CheckerLabels> CheckerLabels::Load(const std::string& labels_dir) {
  if (!util::pypath::IsDir(labels_dir)) {
    return Result<CheckerLabels>(
        Error{.message = labels_dir + " is not a directory."});
  }
  CheckerLabels labels;
  const std::vector<std::string> entries =
      platform::ListDirectory(labels_dir);
  if (std::ranges::find(entries, "descriptions.json") != entries.end()) {
    if (const std::optional<util::JsonDocument> document = LoadJson(
            util::pypath::Join({labels_dir, "descriptions.json"}))) {
      yyjson_val* root = yyjson_doc_get_root(document->get());
      yyjson_obj_iter keys = yyjson_obj_iter_with(root);
      while (yyjson_val* key = yyjson_obj_iter_next(&keys)) {
        auto& values = labels.descriptions_[util::JsonString(key)];
        values.clear();
        yyjson_obj_iter items =
            yyjson_obj_iter_with(yyjson_obj_iter_get_val(key));
        while (yyjson_val* value = yyjson_obj_iter_next(&items)) {
          values.emplace_back(
              util::JsonString(value),
              util::JsonToStr(yyjson_obj_iter_get_val(value)));
        }
      }
    }
  }

  const std::string analyzers_dir =
      util::pypath::Join({labels_dir, "analyzers"});
  for (const std::string& file : platform::ListDirectory(analyzers_dir)) {
    const std::optional<util::JsonDocument> document =
        LoadJson(util::pypath::Join({analyzers_dir, file}));
    if (!document) continue;
    yyjson_val* root = yyjson_doc_get_root(document->get());
    Analyzer analyzer{
        .name = util::JsonToStr(util::JsonGet(root, "analyzer")),
        .checkers = {},
        .index = {}};
    yyjson_obj_iter iter =
        yyjson_obj_iter_with(util::JsonGet(root, "labels"));
    while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
      const std::string name = util::JsonString(key);
      Result<std::vector<Label>> checker_labels =
          CheckerLabelList(name, yyjson_obj_iter_get_val(key));
      if (!checker_labels.ok()) {
        return Result<CheckerLabels>(checker_labels.error());
      }
      // json.loads() keeps the last value of a repeated key, at the place
      // of the first one.
      const auto [it, inserted] =
          analyzer.index.emplace(name, analyzer.checkers.size());
      if (inserted) {
        analyzer.checkers.push_back(
            {.name = name, .labels = std::move(checker_labels).value()});
      } else {
        analyzer.checkers[it->second].labels =
            std::move(checker_labels).value();
      }
    }
    const auto known = std::ranges::find(labels.analyzers_, analyzer.name,
                                         &Analyzer::name);
    if (known != labels.analyzers_.end()) {
      *known = std::move(analyzer);
    } else {
      labels.analyzers_.push_back(std::move(analyzer));
    }
  }

  std::set<Label> seen;
  for (const Analyzer& analyzer : labels.analyzers_) {
    for (const Checker& checker : analyzer.checkers) {
      for (const Label& label : checker.labels) {
        if (!labels.values_.contains(label.first)) {
          labels.label_keys_.push_back(label.first);
        }
        if (seen.insert(label).second) {
          labels.values_[label.first].push_back(label.second);
        }
      }
    }
  }
  return Result<CheckerLabels>(std::move(labels));
}

std::vector<std::string> CheckerLabels::CheckersByLabels(
    const std::vector<std::string>& filter_labels,
    std::optional<std::string_view> analyzer) const {
  std::vector<Label> wanted;
  for (const std::string& filter_label : filter_labels) {
    wanted.push_back(SplitLabel(filter_label));
  }
  std::vector<std::string> result;
  for (const Analyzer& data : analyzers_) {
    if (analyzer && data.name != *analyzer) continue;
    for (const Checker& checker : data.checkers) {
      const bool match = std::ranges::any_of(
          checker.labels, [&wanted](const Label& label) {
            return std::ranges::find(wanted, label) != wanted.end();
          });
      if (match) result.push_back(checker.name);
    }
  }
  return result;
}

std::vector<std::string> CheckerLabels::Analyzers() const {
  std::vector<std::string> names;
  for (const Analyzer& data : analyzers_) names.push_back(data.name);
  return names;
}

std::vector<std::string> CheckerLabels::Checkers(
    std::optional<std::string_view> analyzer) const {
  std::vector<std::string> result;
  for (const Analyzer& data : analyzers_) {
    if (analyzer && data.name != *analyzer) continue;
    for (const Checker& checker : data.checkers) {
      result.push_back(checker.name);
    }
  }
  return result;
}

const std::vector<std::string>& CheckerLabels::OccurringValues(
    std::string_view key) const {
  static const std::vector<std::string>& kNone = *new std::vector<std::string>;
  const auto values = values_.find(key);
  return values == values_.end() ? kNone : values->second;
}

const CheckerLabels::Checker* CheckerLabels::FindChecker(
    const Analyzer& data, const std::string& checker) {
  if (const auto it = data.index.find(checker); it != data.index.end()) {
    return &data.checkers[it->second];
  }
  for (const Checker& candidate : data.checkers) {
    if (checker.starts_with(candidate.name)) return &candidate;
  }
  return nullptr;
}

std::vector<Label> CheckerLabels::AllLabelsOfChecker(
    const std::string& checker,
    std::optional<std::string_view> analyzer) const {
  std::vector<Label> labels;
  for (const Analyzer& data : analyzers_) {
    if (analyzer && data.name != *analyzer) continue;
    const Checker* found = FindChecker(data, checker);
    if (found == nullptr) continue;
    for (const Label& label : found->labels) {
      if (std::ranges::find(labels, label) == labels.end()) {
        labels.push_back(label);
      }
    }
  }
  return labels;
}

std::vector<std::string> CheckerLabels::LabelsOfChecker(
    const std::string& checker, std::string_view key,
    std::optional<std::string_view> analyzer) const {
  std::vector<std::string> values;
  for (const auto& [label_key, value] : AllLabelsOfChecker(checker, analyzer)) {
    if (label_key == key) values.push_back(value);
  }
  return values;
}

std::string CheckerLabels::UniqueLabelOfChecker(
    const std::string& checker, std::string_view key,
    std::optional<std::string_view> analyzer) const {
  const std::vector<std::string> values =
      LabelsOfChecker(checker, key, analyzer);
  if (!values.empty()) return values.front();
  for (const auto& [unique, default_value] : kUniqueLabels) {
    if (unique == key) return std::string(default_value);
  }
  return "";
}

const std::vector<std::pair<std::string, std::string>>&
CheckerLabels::Description(std::string_view key) const {
  static const auto& kNone =
      *new std::vector<std::pair<std::string, std::string>>;
  const auto description = descriptions_.find(key);
  return description == descriptions_.end() ? kNone : description->second;
}

}  // namespace cc::common
