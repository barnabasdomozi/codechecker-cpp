#include "server/package_data.h"

#include <stddef.h>
#include <yyjson.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/checker_labels.h"
#include "common/guidelines.h"
#include "common/package.h"
#include "common/util.h"
#include "util/json.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"

namespace cc::server {
namespace {

// Returns the texts of the system comment kinds in the JSON file `path`;
// none if it cannot be read (Python: load_json(path, {})).
std::vector<SystemCommentKind> LoadSystemComments(const std::string& path) {
  std::vector<SystemCommentKind> comments;
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  if (!document) return comments;
  yyjson_val* root = yyjson_doc_get_root(document->get());
  size_t index = 0;
  size_t count = 0;
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  yyjson_obj_foreach(root, index, count, key, value) {
    comments.push_back(
        {.kind = util::JsonString(key), .text = util::JsonString(value)});
  }
  return comments;
}

// Returns the commit URL patterns in the JSON file `path`; none if it cannot
// be read (Python: __get_git_commit_urls()).
Result<std::vector<GitCommitUrl>> LoadGitCommitUrls(const std::string& path) {
  using Urls = Result<std::vector<GitCommitUrl>>;
  std::vector<GitCommitUrl> urls;
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  if (!document) return Urls(std::move(urls));
  size_t index = 0;
  size_t count = 0;
  yyjson_val* entry = nullptr;
  yyjson_arr_foreach(yyjson_doc_get_root(document->get()), index, count,
                     entry) {
    Result<yyjson_val*> regex = util::JsonSubscript(entry, "regex");
    if (!regex.ok()) return Urls(regex.error());
    Result<util::Regex> compiled =
        util::Regex::Compile(util::JsonString(regex.value()));
    if (!compiled.ok()) return Urls(compiled.error());
    Result<yyjson_val*> url = util::JsonSubscript(entry, "url");
    if (!url.ok()) return Urls(url.error());
    urls.push_back({.regex = std::move(compiled).value(),
                    .url = util::JsonString(url.value())});
  }
  return Urls(std::move(urls));
}

}  // namespace

Result<PackageData> LoadPackageData() {
  using Data = Result<PackageData>;
  const std::string config_dir =
      util::pypath::Join({common::DataFilesDir(), "config"});
  Result<common::CheckerLabels> labels =
      common::CheckerLabels::Load(common::LabelsDirectory());
  if (!labels.ok()) return Data(labels.error());
  Result<common::Guidelines> guidelines = common::Guidelines::Load(
      util::pypath::Join({config_dir, "guidelines"}));
  if (!guidelines.ok()) return Data(guidelines.error());
  std::vector<SystemCommentKind> system_comments = LoadSystemComments(
      util::pypath::Join({config_dir, "system_comment_kinds.json"}));
  Result<std::vector<GitCommitUrl>> urls = LoadGitCommitUrls(
      util::pypath::Join({config_dir, "git_commit_urls.json"}));
  if (!urls.ok()) return Data(urls.error());
  return Data(PackageData{.checker_labels = std::move(labels).value(),
                          .guidelines = std::move(guidelines).value(),
                          .system_comments = std::move(system_comments),
                          .git_commit_urls = std::move(urls).value()});
}

}  // namespace cc::server
