#include "server/metadata.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "api/ordered_map.h"
#include "common/util.h"
#include "util/json.h"
#include "util/pypath.h"
#include "util/strings.h"

namespace cc::server {
namespace {

// AnalyzerStatistics while the tools of a version 2 metadata.json are
// merged.
struct MergedStatistics {
  std::set<std::string> versions;
  std::vector<std::string> failed_sources;
  std::optional<std::vector<std::string>> successful_sources;
  int64_t successful = 0;
};

// Returns the strings of the array `value`; nullopt if it is not an array.
std::optional<std::vector<std::string>> Strings(yyjson_val* value) {
  if (!yyjson_is_arr(value)) return std::nullopt;
  std::vector<std::string> strings;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* item = nullptr;
  yyjson_arr_foreach(value, index, count, item) {
    strings.push_back(util::JsonString(item));
  }
  return strings;
}

// Returns `strings` without repetitions (Python: list(set(strings))).
std::vector<std::string> Unique(const std::vector<std::string>& strings) {
  const std::set<std::string> unique(strings.begin(), strings.end());
  return std::vector<std::string>(unique.begin(), unique.end());
}

// Returns end - begin of the "timestamps" object `timestamps`.
double Duration(yyjson_val* timestamps) {
  return yyjson_get_num(util::JsonGet(timestamps, "end")) -
         yyjson_get_num(util::JsonGet(timestamps, "begin"));
}

// Adds the checkers of the object `source`, name to enabled, to `dest`
// (Python: __insert_checkers()).
void InsertCheckers(yyjson_val* source,
                    cc::api::OrderedMap<std::string, bool>* dest) {
  size_t index = 0;
  size_t count = 0;
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  yyjson_obj_foreach(source, index, count, key, value) {
    (*dest)[util::JsonString(key)] = util::JsonTruthy(value);
  }
}

// Adds the analyzer statistics `source` of `analyzer` to `merged` (Python:
// __insert_analyzer_statistics()).
void InsertStatistics(yyjson_val* source, const std::string& analyzer,
                      cc::api::OrderedMap<std::string, MergedStatistics>*
                          merged) {
  if (!util::JsonTruthy(source)) return;
  const bool known = merged->find(analyzer) != merged->end();
  MergedStatistics& dest = (*merged)[analyzer];
  dest.versions.insert(util::JsonString(util::JsonGet(source, "version")));
  dest.successful +=
      util::JsonToInt(util::JsonGet(source, "successful")).value_or(0);
  for (std::string& path :
       Strings(util::JsonGet(source, "failed_sources")).value_or(
           std::vector<std::string>())) {
    dest.failed_sources.push_back(std::move(path));
  }
  std::optional<std::vector<std::string>> successful_sources =
      Strings(util::JsonGet(source, "successful_sources"));
  // Before CodeChecker 6.16.0 the metadata had no successful_sources; the
  // first tool decides whether the list is kept.
  if (!known) {
    dest.successful_sources = std::move(successful_sources);
  } else if (dest.successful_sources && successful_sources) {
    dest.successful_sources->insert(dest.successful_sources->end(),
                                    successful_sources->begin(),
                                    successful_sources->end());
  }
}

// Reads a metadata.json of version 2 (Python: __process_metadata_info_v2()).
void ParseVersion2(yyjson_val* root, MetadataInfo* info) {
  std::set<std::string> cc_versions;
  std::set<std::string> check_commands;
  cc::api::OrderedMap<std::string, MergedStatistics> statistics;
  size_t index = 0;
  size_t count = 0;
  yyjson_val* tool = nullptr;
  yyjson_arr_foreach(util::JsonGet(root, "tools"), index, count, tool) {
    const std::string name = util::JsonString(util::JsonGet(tool, "name"));
    yyjson_val* version = util::JsonGet(tool, "version");
    if (name == "codechecker" && version != nullptr) {
      cc_versions.insert(util::JsonString(version));
    }
    if (const std::optional<std::vector<std::string>> command =
            Strings(util::JsonGet(tool, "command"))) {
      check_commands.insert(util::Join(*command, " "));
    }
    if (yyjson_val* timestamps = util::JsonGet(tool, "timestamps")) {
      info->check_durations.push_back(Duration(timestamps));
    }
    yyjson_val* analyzers = util::JsonGet(tool, "analyzers");
    if (analyzers == nullptr) {
      InsertStatistics(util::JsonGet(tool, "analyzer_statistics"), name,
                       &statistics);
      InsertCheckers(util::JsonGet(tool, "checkers"), &info->checkers[name]);
      continue;
    }
    size_t analyzer_index = 0;
    size_t analyzer_count = 0;
    yyjson_val* analyzer = nullptr;
    yyjson_val* analyzer_info = nullptr;
    yyjson_obj_foreach(analyzers, analyzer_index, analyzer_count, analyzer,
                       analyzer_info) {
      const std::string analyzer_name = util::JsonString(analyzer);
      InsertStatistics(util::JsonGet(analyzer_info, "analyzer_statistics"),
                       analyzer_name, &statistics);
      InsertCheckers(util::JsonGet(analyzer_info, "checkers"),
                     &info->checkers[analyzer_name]);
    }
  }
  info->check_commands.assign(check_commands.begin(), check_commands.end());
  // The same file may be listed by several report folders, so the lists
  // and the counts are made unique.
  for (auto& [analyzer, merged] : statistics) {
    AnalyzerStatistics& stats = info->analyzer_statistics[analyzer];
    stats.version = util::Join(
        std::vector<std::string>(merged.versions.begin(),
                                 merged.versions.end()),
        "; ");
    stats.failed_sources = Unique(merged.failed_sources);
    stats.successful = merged.successful;
    if (merged.successful_sources) {
      stats.successful_sources = Unique(*merged.successful_sources);
      stats.successful =
          static_cast<int64_t>(stats.successful_sources->size());
    }
  }
  // The checkers of several report folders cannot be told apart, so none
  // are kept: the detection statuses "off" and "unavailable" are not used.
  if (util::JsonToInt(util::JsonGet(root, "num_of_report_dir")).value_or(0) >
      1) {
    info->checkers.clear();
  }
  if (!cc_versions.empty()) {
    info->cc_version = util::Join(
        std::vector<std::string>(cc_versions.begin(), cc_versions.end()),
        "; ");
  }
}

// Reads a metadata.json of version 1 (Python: __process_metadata_info_v1()).
void ParseVersion1(yyjson_val* root, MetadataInfo* info) {
  if (const std::optional<std::vector<std::string>> command =
          Strings(util::JsonGet(root, "command"))) {
    info->check_commands.push_back(util::Join(*command, " "));
  }
  if (yyjson_val* timestamps = util::JsonGet(root, "timestamps")) {
    info->check_durations.push_back(Duration(timestamps));
  }
  yyjson_val* version =
      util::JsonGet(util::JsonGet(root, "versions"), "codechecker");
  if (yyjson_is_str(version)) info->cc_version = util::JsonString(version);
  size_t index = 0;
  size_t count = 0;
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  yyjson_obj_foreach(util::JsonGet(root, "analyzer_statistics"), index, count,
                     key, value) {
    AnalyzerStatistics& stats =
        info->analyzer_statistics[util::JsonString(key)];
    if (yyjson_val* analyzer_version = util::JsonGet(value, "version")) {
      stats.version = util::JsonString(analyzer_version);
    }
    stats.failed_sources = Strings(util::JsonGet(value, "failed_sources"));
    stats.successful_sources =
        Strings(util::JsonGet(value, "successful_sources"));
    stats.successful = util::JsonToInt(util::JsonGet(value, "successful"));
  }
  // Old files list only the enabled checkers of each analyzer.
  yyjson_obj_foreach(util::JsonGet(root, "checkers"), index, count, key,
                     value) {
    cc::api::OrderedMap<std::string, bool>& checkers =
        info->checkers[util::JsonString(key)];
    if (yyjson_is_arr(value)) {
      // A named copy: a range-for would not keep the optional alive.
      const std::vector<std::string> names = *Strings(value);
      for (const std::string& checker : names) {
        checkers[checker] = true;
      }
    } else {
      InsertCheckers(value, &checkers);
    }
  }
}

}  // namespace

MetadataInfo ParseMetadata(const std::string& path) {
  MetadataInfo info;
  if (!util::pypath::IsFile(path)) return info;
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  yyjson_val* root =
      document ? yyjson_doc_get_root(document->get()) : nullptr;
  if (util::JsonGet(root, "version") != nullptr) {
    ParseVersion2(root, &info);
  } else {
    ParseVersion1(root, &info);
  }
  // Python: __process_metadata_checkers().
  for (const auto& [analyzer, checkers] : info.checkers) {
    for (const auto& [checker, enabled] : checkers) {
      info.checker_to_analyzer[checker] = analyzer;
      if (enabled) {
        info.enabled_checkers.insert(checker);
      } else {
        info.disabled_checkers.insert(checker);
      }
    }
  }
  return info;
}

bool CheckerIsUnavailable(const std::string& checker_name,
                          const std::set<std::string>& enabled_checkers) {
  return !checker_name.starts_with("clang-diagnostic-") &&
         !enabled_checkers.empty() && !enabled_checkers.contains(checker_name);
}

}  // namespace cc::server
