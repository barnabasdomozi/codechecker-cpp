#include "common/guidelines.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/util.h"
#include "platform/filesystem.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::common {
namespace {

constexpr util::Logger kLog("system");

// Returns the text of the string member `key` of the object `rule`; "" if
// it is missing.
std::string Field(yyjson_val* rule, std::string_view key) {
  return util::JsonString(util::JsonGet(rule, key));
}

// Returns the rules of the guideline file `data`, or why it is not in the
// guideline format (Python: __check_guideline_format()).
Result<std::vector<Guidelines::Rule>> ReadRules(yyjson_val* data) {
  using Rules = Result<std::vector<Guidelines::Rule>>;
  auto format_error = [](std::string message) {
    return Rules(Error{.message = std::move(message)});
  };
  if (!yyjson_is_str(util::JsonGet(data, "guideline"))) {
    return format_error("The 'guideline' field must exist and be a string.");
  }
  if (!yyjson_is_str(util::JsonGet(data, "guideline_title"))) {
    return format_error(
        "The 'guideline_title' field must exist and be a string.");
  }
  yyjson_val* rules = util::JsonGet(data, "rules");
  if (!yyjson_is_arr(rules)) {
    return format_error(
        "The 'rules' field must exist and be a list of dictionaris.");
  }
  size_t index = 0;
  size_t count = 0;
  yyjson_val* rule = nullptr;
  yyjson_arr_foreach(rules, index, count, rule) {
    if (!yyjson_is_obj(rule)) {
      return format_error(
          "The 'rules' field must exist and be a list of dictionaris.");
    }
  }
  std::vector<Guidelines::Rule> result;
  yyjson_arr_foreach(rules, index, count, rule) {
    if (!yyjson_is_str(util::JsonGet(rule, "rule_id"))) {
      return format_error("All rules must have 'rule_id' that is a string.");
    }
    Guidelines::Rule parsed = {.id = Field(rule, "rule_id"),
                               .title = Field(rule, "title"),
                               .url = Field(rule, "rule_url"),
                               .level = Field(rule, "level")};
    // Python keeps the rules in a dict: a repeated id replaces the rule.
    const auto same = std::ranges::find(result, parsed.id,
                                        &Guidelines::Rule::id);
    if (same != result.end()) {
      *same = std::move(parsed);
    } else {
      result.push_back(std::move(parsed));
    }
  }
  return Rules(std::move(result));
}

}  // namespace

Result<Guidelines> Guidelines::Load(const std::string& guidelines_dir) {
  if (!util::pypath::IsDir(guidelines_dir)) {
    return Result<Guidelines>(Error{
        .message = std::format("{} is not a directory.", guidelines_dir)});
  }
  Guidelines guidelines;
  for (const std::string& name : platform::ListDirectory(guidelines_dir)) {
    if (name.ends_with(".license")) continue;
    const std::string path = util::pypath::Join({guidelines_dir, name});
    const std::optional<util::JsonDocument> document = LoadYaml(path);
    yyjson_val* data = document ? yyjson_doc_get_root(document->get())
                                : nullptr;
    Result<std::vector<Rule>> rules = ReadRules(data);
    if (!rules.ok()) {
      kLog.Warning(std::format(
          "{} does not have a correct guideline format.", path));
      kLog.Warning(rules.error().message);
      continue;
    }
    guidelines.rules_[Field(data, "guideline")] = std::move(rules).value();
  }
  return Result<Guidelines>(std::move(guidelines));
}

const std::vector<Guidelines::Rule>& Guidelines::RulesOfGuideline(
    std::string_view name) const {
  static const auto& kNone = *new std::vector<Rule>;
  const auto rules = rules_.find(name);
  return rules == rules_.end() ? kNone : rules->second;
}

}  // namespace cc::common
