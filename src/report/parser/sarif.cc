#include "report/parser/sarif.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

// Returns the path of `uri` as os.path.join(netloc, path) of Python's
// urlparse(uri) gives it.
std::string UriPath(std::string uri) {
  std::erase_if(uri,
                [](char c) { return c == '\t' || c == '\r' || c == '\n'; });
  const size_t first = uri.find_first_not_of(
      std::string_view("\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f"
                       "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c"
                       "\x1d\x1e\x1f \0",
                       33));
  std::string_view url = std::string_view(uri).substr(
      first == std::string::npos ? uri.size() : first);
  std::string scheme;
  if (const size_t colon = url.find(':');
      colon != std::string_view::npos && colon > 0 &&
      ((url[0] >= 'a' && url[0] <= 'z') || (url[0] >= 'A' && url[0] <= 'Z')) &&
      url.substr(0, colon).find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.") ==
          std::string_view::npos) {
    for (const char c : url.substr(0, colon)) {
      scheme += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    url.remove_prefix(colon + 1);
  }
  std::string_view netloc;
  if (url.starts_with("//")) {
    const size_t end = std::min(url.find_first_of("/?#", 2), url.size());
    netloc = url.substr(2, end - 2);
    url.remove_prefix(end);
  }
  url = url.substr(0, url.find('#'));
  url = url.substr(0, url.find('?'));
  // Schemes in urllib's uses_params have ";params" after the last segment.
  constexpr std::string_view kUsesParams[] = {
      "", "ftp", "hdl", "prospero", "http", "imap", "https", "shttp", "rtsp",
      "rtsps", "rtspu", "sip", "sips", "mms", "sftp", "tel"};
  if (std::ranges::find(kUsesParams, scheme) != std::end(kUsesParams)) {
    const size_t slash = url.rfind('/');
    url = url.substr(
        0, url.find(';', slash == std::string_view::npos ? 0 : slash));
  }
  return util::pypath::Join({netloc, url});
}

// Formats `text` like Python's str.format(*arguments): {0}, {1} and {}
// placeholders, and {{ and }} for braces.
std::string FormatMessage(std::string_view text,
                          const std::vector<std::string>& arguments) {
  std::string result;
  size_t automatic = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if ((text[i] == '{' || text[i] == '}') && i + 1 < text.size() &&
        text[i + 1] == text[i]) {
      result += text[i++];
      continue;
    }
    if (text[i] != '{') {
      result += text[i];
      continue;
    }
    const size_t close = text.find('}', i);
    if (close == std::string_view::npos) break;
    std::string_view field = text.substr(i + 1, close - i - 1);
    field = field.substr(0, field.find_first_of(":!"));
    size_t index = automatic++;
    if (!field.empty()) {
      std::from_chars(field.data(), field.data() + field.size(), index);
    }
    if (index < arguments.size()) result += arguments[index];
    i = close;
  }
  return result;
}

// The state of reading one SARIF file (Python: sarif.Parser).
class SarifReader {
 public:
  explicit SarifReader(const std::string* path) : path_(path) {}

  SarifReader(const SarifReader&) = delete;
  SarifReader& operator=(const SarifReader&) = delete;

  std::vector<Report> Read(yyjson_val* root);

 private:
  std::string Message(yyjson_val* message, const std::string& rule_id,
                      yyjson_val* rules) const;
  // Python: _process_location(); nullopt if it has no usable physical
  // location.
  std::optional<std::pair<File, Range>> PhysicalLocation(
      yyjson_val* location);
  std::string ResolveUriBaseId(const std::string& id,
                               const std::string& postfix);

  const std::string* path_;
  yyjson_val* original_uri_base_ids_ = nullptr;
  bool had_error_ = false;
};

std::string SarifReader::Message(yyjson_val* message,
                                 const std::string& rule_id,
                                 yyjson_val* rules) const {
  if (yyjson_val* text = util::JsonGet(message, "text")) {
    return util::JsonToStr(text);
  }
  std::vector<std::string> arguments;
  yyjson_arr_iter iter =
      yyjson_arr_iter_with(util::JsonGet(message, "arguments"));
  while (yyjson_val* argument = yyjson_arr_iter_next(&iter)) {
    arguments.push_back(util::JsonToStr(argument));
  }
  // The rule with this id; the last one counts, as in a Python dict.
  yyjson_val* rule = nullptr;
  iter = yyjson_arr_iter_with(rules);
  while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
    if (util::JsonToStr(util::JsonGet(item, "id")) == rule_id) rule = item;
  }
  yyjson_val* strings = util::JsonGet(rule, "messageStrings");
  yyjson_val* entry =
      util::JsonGet(strings, util::JsonToStr(util::JsonGet(message, "id")));
  return FormatMessage(util::JsonToStr(util::JsonGet(entry, "text")),
                       arguments);
}

std::string SarifReader::ResolveUriBaseId(const std::string& id,
                                          const std::string& postfix) {
  const std::string error = std::format(
      "Failed to fully resolve the path for '{}'. sarif v2.1.0 permits this, "
      "but CodeChecker requires it to be available. If you intend to hide "
      "the absolute path before storing the results to a CodeChecker "
      "server, use --trim-path-prefix.",
      postfix);
  if (original_uri_base_ids_ == nullptr ||
      yyjson_obj_size(original_uri_base_ids_) == 0) {
    kLog.Error(std::format(
        "Missing 'originalUriBaseIds' (sarif v2.1.0 §3.14.14) in '{}'.",
        *path_));
    kLog.Error(error);
    had_error_ = true;
    return "";
  }
  yyjson_val* original = util::JsonGet(original_uri_base_ids_, id);
  if (original == nullptr || (yyjson_is_obj(original) &&
                              yyjson_obj_size(original) == 0)) {
    kLog.Error(std::format(
        "Missing entry for '{} in 'originalUriBaseIds' (sarif v2.1.0 "
        "§3.14.14) in '{}'.",
        id, *path_));
    kLog.Error(error);
    had_error_ = true;
    return "";
  }
  yyjson_val* uri = util::JsonGet(original, "uri");
  if (uri == nullptr || util::JsonToStr(uri).empty()) {
    kLog.Warning(std::format(
        "Missing 'uri' (sarif v2.1.0 §3.4.3) for '{} in 'originalUriBaseIds' "
        "(sarif v2.1.0 §3.14.14) in '{}'.",
        id, *path_));
    kLog.Error(error);
    had_error_ = true;
    return "";
  }
  // Python looks for "uri_base_id" here, not "uriBaseId".
  if (yyjson_val* base = util::JsonGet(original, "uri_base_id")) {
    return ResolveUriBaseId(util::JsonToStr(base), postfix) +
           util::JsonToStr(uri);
  }
  return util::JsonToStr(uri);
}

std::optional<std::pair<File, Range>> SarifReader::PhysicalLocation(
    yyjson_val* location) {
  yyjson_val* physical = util::JsonGet(location, "physicalLocation");
  if (physical == nullptr ||
      (yyjson_is_obj(physical) && yyjson_obj_size(physical) == 0)) {
    return std::nullopt;
  }
  std::optional<File> file;
  yyjson_val* artifact = util::JsonGet(physical, "artifactLocation");
  if (artifact != nullptr && yyjson_obj_size(artifact) > 0) {
    std::string uri = util::JsonToStr(util::JsonGet(artifact, "uri"));
    if (yyjson_val* base = util::JsonGet(artifact, "uriBaseId")) {
      uri = ResolveUriBaseId(util::JsonToStr(base), uri) + uri;
    }
    file = MakeFile(UriPath(uri));
  }
  yyjson_val* region = util::JsonGet(physical, "region");
  yyjson_val* start_line_value = util::JsonGet(region, "startLine");
  if (!file || start_line_value == nullptr ||
      yyjson_is_null(start_line_value)) {
    return std::nullopt;
  }
  const auto get = [region](const char* key, int otherwise) {
    yyjson_val* value = util::JsonGet(region, key);
    if (value == nullptr) return otherwise;
    return static_cast<int>(util::JsonToInt(value).value_or(otherwise));
  };
  const int start_line =
      static_cast<int>(util::JsonToInt(start_line_value).value_or(0));
  const int start_col = get("startColumn", 1);
  return std::pair<File, Range>(
      *file, Range{start_line, start_col, get("endLine", start_line),
                   get("endColumn", start_col)});
}

std::vector<Report> SarifReader::Read(yyjson_val* root) {
  std::vector<Report> reports;
  SourceFileCache sources;
  yyjson_arr_iter runs = yyjson_arr_iter_with(util::JsonGet(root, "runs"));
  while (yyjson_val* run = yyjson_arr_iter_next(&runs)) {
    yyjson_val* driver =
        util::JsonGet(util::JsonGet(run, "tool"), "driver");
    yyjson_val* rules = util::JsonGet(driver, "rules");
    yyjson_val* name = util::JsonGet(driver, "name");
    const std::string analyzer_name =
        name != nullptr ? util::JsonToStr(name) : "unknown";
    original_uri_base_ids_ = util::JsonGet(run, "originalUriBaseIds");
    yyjson_val* results = util::JsonGet(run, "results");
    if (results == nullptr) continue;
    yyjson_arr_iter result_iter = yyjson_arr_iter_with(results);
    while (yyjson_val* result = yyjson_arr_iter_next(&result_iter)) {
      const std::string rule_id =
          util::JsonToStr(util::JsonGet(result, "ruleId"));
      const std::string message =
          Message(util::JsonGet(result, "message"), rule_id, rules);

      // Python: _process_code_flows().
      std::vector<BugPathEvent> events;
      yyjson_arr_iter flows =
          yyjson_arr_iter_with(util::JsonGet(result, "codeFlows"));
      while (yyjson_val* flow = yyjson_arr_iter_next(&flows)) {
        yyjson_arr_iter threads =
            yyjson_arr_iter_with(util::JsonGet(flow, "threadFlows"));
        while (yyjson_val* thread = yyjson_arr_iter_next(&threads)) {
          yyjson_arr_iter items =
              yyjson_arr_iter_with(util::JsonGet(thread, "locations"));
          while (yyjson_val* item = yyjson_arr_iter_next(&items)) {
            yyjson_val* location = util::JsonGet(item, "location");
            yyjson_val* event_message = util::JsonGet(location, "message");
            if (event_message == nullptr) continue;
            const std::string text = Message(event_message, rule_id, rules);
            std::optional<std::pair<File, Range>> place =
                PhysicalLocation(location);
            if (!place) continue;
            events.push_back({.message = text,
                              .file = place->first,
                              .line = place->second.start_line,
                              .column = place->second.start_col,
                              .range = place->second});
          }
        }
      }

      yyjson_arr_iter locations =
          yyjson_arr_iter_with(util::JsonGet(result, "locations"));
      while (yyjson_val* location = yyjson_arr_iter_next(&locations)) {
        std::optional<std::pair<File, Range>> place =
            PhysicalLocation(location);
        if (!place) continue;
        if (had_error_) return {};
        Report report =
            MakeReport(place->first, place->second.start_line,
                       place->second.start_col, message, rule_id,
                       events.empty()
                           ? std::nullopt
                           : std::optional<std::vector<BugPathEvent>>(events));
        report.analyzer_name = analyzer_name;
        report.analyzer_result_file_path = *path_;
        report.report_hash =
            GetReportHash(report, HashType::kPathSensitive, sources);
        reports.push_back(std::move(report));
      }
    }
  }
  return reports;
}

}  // namespace

std::vector<Report> ReadSarifFile(const std::string& path) {
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    kLog.Warning("Failed to open json file: " + path);
    kLog.Warning(content.error().message);
    return {};
  }
  const Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(content.value()));
  if (!document.ok()) {
    kLog.Warning(path + " is not a valid json file.");
    kLog.Warning(document.error().message);
    return {};
  }
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  if (yyjson_arr_size(util::JsonGet(root, "runs")) == 0) return {};
  SarifReader reader(&path);
  return reader.Read(root);
}

}  // namespace cc::report
