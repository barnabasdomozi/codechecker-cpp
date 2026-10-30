#include "report/output/gerrit.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/subprocess.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

// Returns the changed files of the Gerrit changed files JSON at `path`
// (Python: __get_changed_files()): the keys of its first {...} block,
// which may follow other text, except the commit message. Empty if the
// file does not exist.
Result<std::vector<std::string>> GerritChangedFiles(const std::string& path) {
  using Files = Result<std::vector<std::string>>;
  std::vector<std::string> files;
  if (path.empty() || !util::pypath::Exists(path)) return Files(files);
  const Result<std::string> content = util::ReadTextFile(path);
  if (!content.ok()) return Files(content.error());
  // Python's regex \{[\s\S]*\}: from the first "{" to the last "}".
  const std::string& text = content.value();
  const size_t begin = text.find('{');
  const size_t end = text.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end < begin) {
    return Files(files);
  }
  const Result<util::JsonDocument> document =
      util::ParseJson(std::string_view(text).substr(begin, end - begin + 1));
  if (!document.ok()) return Files(document.error());
  yyjson_obj_iter iter =
      yyjson_obj_iter_with(yyjson_doc_get_root(document.value().get()));
  while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
    std::string file = util::JsonString(key);
    if (file.find("/COMMIT_MSG") == std::string::npos) {
      files.push_back(std::move(file));
    }
  }
  return Files(std::move(files));
}

// Returns the review comment `message` at the location of `report`.
yyjson_mut_val* Comment(yyjson_mut_doc* doc, const Report& report,
                        const std::string& message) {
  yyjson_mut_val* range = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, range, "start_line", report.line);
  yyjson_mut_obj_add_int(doc, range, "start_character", report.column);
  yyjson_mut_obj_add_int(doc, range, "end_line", report.line);
  yyjson_mut_obj_add_int(doc, range, "end_character", report.column);
  yyjson_mut_val* comment = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, comment, "range", range);
  yyjson_mut_obj_add_val(doc, comment, "message",
                         util::MakeJsonString(doc, message));
  return comment;
}

}  // namespace

bool GerritEnvironmentIsSet() {
  bool set = true;
  if (!platform::GetEnv("CC_REPO_DIR")) {
    kLog.Error(
        "When using gerrit output the 'CC_REPO_DIR' environment variable "
        "needs to be set to the root directory of the sources, i.e. the "
        "directory where the repository was cloned!");
    set = false;
  }
  if (!platform::GetEnv("CC_CHANGED_FILES")) {
    kLog.Error(
        "When using gerrit output the 'CC_CHANGED_FILES' environment "
        "variable needs to be set to the path of changed files json from "
        "Gerrit!");
    set = false;
  }
  return set;
}

Result<std::string> ReportsToGerrit(const std::vector<Report>& reports) {
  const std::string repo_dir = platform::GetEnv("CC_REPO_DIR").value_or("");
  const std::string report_url =
      platform::GetEnv("CC_REPORT_URL").value_or("");
  const std::string changed_files_path =
      platform::GetEnv("CC_CHANGED_FILES").value_or("");
  const Result<std::vector<std::string>> changed_files =
      GerritChangedFiles(changed_files_path);
  if (!changed_files.ok()) return Result<std::string>(changed_files.error());

  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* comments = yyjson_mut_obj(doc);
  std::vector<std::string> unchanged_file_messages;
  SourceFileCache sources;
  for (const Report& report : reports) {
    const std::string& file_name = report.file.path;
    // The path can be a bare file name in the report.
    const std::string relative_path =
        !repo_dir.empty() && !util::pypath::Dirname(file_name).empty() &&
                file_name.starts_with('/')
            ? util::pypath::Relpath(file_name, repo_dir)
            : file_name;
    const std::string message = std::format(
        "[{}] {}:{}:{}: {} [{}]\n{}", report.severity.value_or("None"),
        relative_path, report.line, report.column, report.message,
        report.checker_name, SourceLine(report, sources));
    if (!changed_files_path.empty() &&
        std::ranges::none_of(
            changed_files.value(), [&file_name](const std::string& changed) {
              return file_name.ends_with(changed);
            })) {
      unchanged_file_messages.push_back(message);
      continue;
    }
    yyjson_mut_val* file_comments = yyjson_mut_obj_getn(
        comments, relative_path.data(), relative_path.size());
    if (file_comments == nullptr) {
      file_comments = yyjson_mut_arr(doc);
      yyjson_mut_obj_add(comments, util::MakeJsonString(doc, relative_path),
                         file_comments);
    }
    yyjson_mut_arr_append(file_comments, Comment(doc, report, message));
  }

  std::string message = std::format(
      "CodeChecker found {} issue(s) in the code.", reports.size());
  if (!unchanged_file_messages.empty()) {
    message +=
        "\n\nThere following reports are introduced in files which are not "
        "changed and can't be shown as individual reports:\n" +
        util::Join(unchanged_file_messages, "\n") + "\n";
  }
  if (!report_url.empty()) message += " See: " + report_url;
  const int label = reports.empty() ? 1 : -1;
  yyjson_mut_val* labels = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, labels, "Code-Review", label);
  yyjson_mut_obj_add_int(doc, labels, "Verified", label);
  yyjson_mut_val* review = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, review, "tag", "jenkins");
  yyjson_mut_obj_add_val(doc, review, "message",
                         util::MakeJsonString(doc, message));
  yyjson_mut_obj_add_val(doc, review, "labels", labels);
  yyjson_mut_obj_add_val(doc, review, "comments", comments);
  return Result<std::string>(util::DumpJson(review));
}

}  // namespace cc::report
