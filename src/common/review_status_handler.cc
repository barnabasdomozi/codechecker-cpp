#include "common/review_status_handler.h"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/source_code_comment_handler.h"
#include "common/util.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "util/file.h"
#include "util/fnmatch.h"
#include "util/json.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/yaml.h"

namespace cc::common {
namespace {

constexpr std::array<std::string_view, 4> kReviewStatusOptions = {
    "false_positive", "suppress", "confirmed", "intentional"};
constexpr std::array<std::string_view, 3> kAllowedFilters = {
    "filepath", "checker_name", "report_hash"};
constexpr std::array<std::string_view, 2> kAllowedActions = {"review_status",
                                                             "reason"};

std::string JoinViews(std::span<const std::string_view> items) {
  std::string result;
  for (const std::string_view item : items) {
    if (!result.empty()) result += ", ";
    result += item;
  }
  return result;
}

// Returns str(value) as an f-string writes it.
std::string Str(yyjson_val* value) {
  return yyjson_is_arr(value) || yyjson_is_obj(value) ? util::JsonToRepr(value)
                                                       : util::JsonToStr(value);
}

// Returns yaml.dump(value).
std::string Dump(yyjson_val* value) {
  const util::MutableJsonDocument copy = util::NewJsonDocument();
  return util::DumpYaml(yyjson_val_mut_copy(copy.get(), value), 80);
}

// Returns the items Python's `for field in value` visits: the keys of a
// mapping, the items of a list, the characters of a string; nullopt where
// Python raises TypeError.
std::optional<std::vector<std::string>> Iterate(yyjson_val* value) {
  std::vector<std::string> items;
  if (yyjson_is_obj(value)) {
    yyjson_obj_iter iter = yyjson_obj_iter_with(value);
    while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
      items.push_back(util::JsonToStr(key));
    }
  } else if (yyjson_is_arr(value)) {
    yyjson_arr_iter iter = yyjson_arr_iter_with(value);
    while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
      items.push_back(Str(item));
    }
  } else if (yyjson_is_str(value)) {
    const std::string text = util::JsonToStr(value);
    size_t pos = 0;
    while (pos < text.size()) {
      const size_t start = pos;
      util::NextCodePoint(text, &pos);
      items.push_back(text.substr(start, pos - start));
    }
  } else {
    return std::nullopt;
  }
  return items;
}

// Returns Python's `name in value`.
bool Contains(yyjson_val* value, std::string_view name) {
  if (yyjson_is_str(value)) {
    return util::JsonToStr(value).find(name) != std::string::npos;
  }
  if (yyjson_is_obj(value)) return util::JsonGet(value, name) != nullptr;
  const std::optional<std::vector<std::string>> items = Iterate(value);
  return items && std::ranges::find(*items, name) != items->end();
}

bool Allowed(std::span<const std::string_view> allowed,
             const std::string& name) {
  return std::ranges::find(allowed, name) != allowed.end();
}

// Python: __check_format_version_1().
Result<void> CheckFormatVersion1(yyjson_val* data, const std::string& path) {
  yyjson_val* rules = util::JsonGet(data, "rules");
  if (!yyjson_is_arr(rules)) {
    return Result<void>(Error{.message = std::format(
                                  "{} should contain the key 'rules' with a "
                                  "non-empty list of (filters, actions) pair.",
                                  path)});
  }
  yyjson_arr_iter iter = yyjson_arr_iter_with(rules);
  while (yyjson_val* rule = yyjson_arr_iter_next(&iter)) {
    yyjson_val* filters = util::JsonGet(rule, "filters");
    yyjson_val* actions = util::JsonGet(rule, "actions");
    if (filters == nullptr || actions == nullptr) {
      return Result<void>(Error{
          .message = std::format(
              "Format error in {}. The following section should be an "
              "object with keys 'filters' and 'actions':\n{}",
              path, Dump(rule))});
    }
    for (const auto& [name, value] : {std::pair("filters", filters),
                                      std::pair("actions", actions)}) {
      if (yyjson_is_null(value)) {
        return Result<void>(Error{.message = std::format(
                                      "Format error in {}: '{}' must have at "
                                      "least one element.",
                                      path, name)});
      }
    }
    const auto check_fields =
        [&path](yyjson_val* fields, std::string_view kind,
                std::span<const std::string_view> allowed) -> Result<void> {
      const std::optional<std::vector<std::string>> names = Iterate(fields);
      if (!names) {
        return Result<void>(
            Error{.message = std::format("TypeError: '{}' object is not "
                                         "iterable",
                                         yyjson_is_bool(fields) ? "bool"
                                         : yyjson_is_real(fields) ? "float"
                                                                  : "int")});
      }
      for (const std::string& field : *names) {
        if (!Allowed(allowed, field)) {
          return Result<void>(Error{.message = std::format(
              "Format error in {}: {} '{}' is not allowed. Available {}s are: "
              "{}",
              path, kind, field, kind, JoinViews(allowed))});
        }
      }
      return Result<void>();
    };
    if (Result<void> checked = check_fields(filters, "filter", kAllowedFilters);
        !checked.ok()) {
      return checked;
    }
    if (Result<void> checked = check_fields(actions, "action", kAllowedActions);
        !checked.ok()) {
      return checked;
    }
    if (!Contains(actions, "review_status") && !Contains(actions, "ignore")) {
      return Result<void>(Error{
          .message = std::format(
              "Format error in {}. 'review_status' is required in:\n{}.", path,
              Dump(rule))});
    }
    if (Contains(actions, "review_status")) {
      yyjson_val* status = util::JsonGet(actions, "review_status");
      if (status == nullptr) {
        return Result<void>(Error{
            .message = "TypeError: list indices must be integers or slices, "
                       "not str"});
      }
      if (!yyjson_is_str(status) ||
          !Allowed(kReviewStatusOptions, util::JsonToStr(status))) {
        return Result<void>(Error{.message = std::format(
            "Invalid review status field: {} at {} in {}. Available options "
            "are: {}.",
            Str(status), util::JsonToRepr(rule), path,
            JoinViews(kReviewStatusOptions))});
      }
    }
  }
  return Result<void>();
}

// Python: __report_matches_rule().
bool ReportMatchesRule(const report::Report& report, yyjson_val* rule) {
  yyjson_val* filters = util::JsonGet(rule, "filters");
  if (yyjson_val* filepath = util::JsonGet(filters, "filepath");
      filepath != nullptr &&
      !util::FnMatch(report.file.original_path, util::JsonToStr(filepath))) {
    return false;
  }
  if (yyjson_val* checker = util::JsonGet(filters, "checker_name");
      checker != nullptr &&
      (!yyjson_is_str(checker) ||
       report.checker_name != util::JsonToStr(checker))) {
    return false;
  }
  if (yyjson_val* hash = util::JsonGet(filters, "report_hash");
      hash != nullptr && report.report_hash && !report.report_hash->empty() &&
      !report.report_hash->starts_with(util::JsonToStr(hash))) {
    return false;
  }
  return true;
}

}  // namespace

Result<void> ReviewStatusHandler::SetReviewStatusConfig(
    const std::string& path) {
  path_ = path;
  if (util::pypath::IsLink(path)) {
    std::error_code error;
    const std::string target =
        std::filesystem::read_symlink(path, error).string();
    if (error) return Result<void>(util::OsError(error.value(), path));
    if (!util::pypath::Exists(target)) {
      return Result<void>(Error{.message = target + " not found."});
    }
  }
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) return Result<void>(content.error());
  Result<util::JsonDocument> document =
      util::SafeLoadYaml(util::DecodeUtf8Ignore(content.value()), path);
  if (!document.ok()) {
    return Result<void>(Error{.message = std::format(
                                  "Invalid YAML format in {}:\n{}", path,
                                  document.error().message)});
  }
  data_ = std::move(document).value();
  yyjson_val* data = yyjson_doc_get_root(data_->get());
  if (!yyjson_is_obj(data)) {
    return Result<void>(
        Error{.message = path + " should represent a dictionary."});
  }
  yyjson_val* version = util::JsonGet(data, "$version");
  if (version == nullptr) {
    return Result<void>(Error{.message = std::format(
        "{} must contain the key '$version'. Hint: Currently the latest "
        "version of this is 1. Consider adding '$version: 1' to the top of "
        "the file.",
        path)});
  }
  // A bool is an int in Python, and True == 1.
  if (!yyjson_is_int(version) && !yyjson_is_bool(version)) {
    return Result<void>(Error{.message = std::format(
                                  "{} should have an integer value for the "
                                  "key '$version'.",
                                  path)});
  }
  if (util::JsonToInt(version) == 1) return CheckFormatVersion1(data, path);
  return Result<void>();
}

Result<report::ReviewStatus> ReviewStatusHandler::GetReviewStatus(
    const report::Report& report, report::SourceFileCache& sources) {
  Result<std::optional<report::ReviewStatus>> from_source =
      ReviewStatusFromSource(report, sources);
  if (!from_source.ok()) {
    return Result<report::ReviewStatus>(from_source.error());
  }
  if (from_source.value()) {
    return Result<report::ReviewStatus>(std::move(*from_source.value()));
  }
  if (std::optional<report::ReviewStatus> from_config =
          ReviewStatusFromConfig(report)) {
    return Result<report::ReviewStatus>(std::move(*from_config));
  }
  return Result<report::ReviewStatus>(report::ReviewStatus());
}

Result<std::optional<report::ReviewStatus>>
ReviewStatusHandler::ReviewStatusFromSource(const report::Report& report,
                                            report::SourceFileCache& sources) {
  using Status = Result<std::optional<report::ReviewStatus>>;
  const std::string source_file =
      source_root_.empty()
          ? util::pypath::Realpath(report.file.original_path)
          : PathForFakeRoot(report.file.original_path, source_root_);
  if (report::ChangedFiles(report).contains(source_file) ||
      !util::pypath::IsFile(source_file)) {
    return Status(std::nullopt);
  }
  // A quick check before the lines are looked at, once for each file.
  auto marker = has_comment_marker_.find(source_file);
  if (marker == has_comment_marker_.end()) {
    const std::string* text = sources.Text(source_file);
    if (text == nullptr) return Status(std::nullopt);
    marker = has_comment_marker_
                 .emplace(source_file,
                          text->find("codechecker_") != std::string::npos)
                 .first;
  }
  if (!marker->second) return Status(std::nullopt);
  const std::vector<std::string_view>* lines = sources.Lines(source_file);
  if (lines == nullptr) return Status(std::nullopt);
  const Result<std::vector<SourceCodeComment>> comments =
      FilterSourceLineComments(*lines, report.line, report.checker_name);
  if (!comments.ok()) {
    source_comment_warnings_.push_back(source_file + " contains " +
                                       comments.error().message);
    return Status(std::nullopt);
  }
  if (comments.value().size() > 1) {
    return Status(Error{.message = std::format(
                            "Multiple source code comments can be found for "
                            "'{}' checker in '{}' at line {}.",
                            report.checker_name, source_file, report.line)});
  }
  if (comments.value().empty()) return Status(std::nullopt);
  const SourceCodeComment& comment = comments.value().front();
  return Status(report::ReviewStatus{.status = comment.status,
                                     .message = comment.message,
                                     .source_comment = comment.line});
}

std::optional<report::ReviewStatus>
ReviewStatusHandler::ReviewStatusFromConfig(
    const report::Report& report) const {
  if (!data_) return std::nullopt;
  yyjson_val* rules =
      util::JsonGet(yyjson_doc_get_root(data_->get()), "rules");
  yyjson_arr_iter iter = yyjson_arr_iter_with(rules);
  while (yyjson_val* rule = yyjson_arr_iter_next(&iter)) {
    yyjson_val* actions = util::JsonGet(rule, "actions");
    if (!ReportMatchesRule(report, rule) ||
        util::JsonTruthy(util::JsonGet(actions, "ignore"))) {
      continue;
    }
    yyjson_val* filters = util::JsonGet(rule, "filters");
    if (!std::ranges::any_of(kAllowedFilters, [filters](std::string_view key) {
          return Contains(filters, key);
        })) {
      continue;
    }
    std::string status =
        util::JsonToStr(util::JsonGet(actions, "review_status"));
    // Python rewrites "suppress" when it checks the file.
    if (status == "suppress") status = "false_positive";
    yyjson_val* reason = util::JsonGet(actions, "reason");
    return report::ReviewStatus{
        .status = std::move(status),
        .message = reason != nullptr ? util::JsonToStr(reason) : "",
        .source_comment = ""};
  }
  return std::nullopt;
}

bool ReviewStatusHandler::ShouldIgnore(const report::Report& report) const {
  if (!data_) return false;
  yyjson_val* rules =
      util::JsonGet(yyjson_doc_get_root(data_->get()), "rules");
  yyjson_arr_iter iter = yyjson_arr_iter_with(rules);
  while (yyjson_val* rule = yyjson_arr_iter_next(&iter)) {
    if (ReportMatchesRule(report, rule) &&
        util::JsonTruthy(
            util::JsonGet(util::JsonGet(rule, "actions"), "ignore"))) {
      return true;
    }
  }
  return false;
}

}  // namespace cc::common
