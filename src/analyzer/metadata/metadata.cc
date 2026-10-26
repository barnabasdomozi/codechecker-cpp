#include "analyzer/metadata/metadata.h"

#include <yyjson.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/util.h"
#include "util/json.h"
#include "util/strings.h"

namespace cc::analyzer {
namespace {

using SourceFiles = std::vector<std::pair<std::string, std::string>>;

void AddObject(yyjson_val* object, SourceFiles* files) {
  yyjson_obj_iter iter = yyjson_obj_iter_with(object);
  while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
    const std::string result_file = util::JsonToStr(key);
    const std::string source = util::JsonToStr(yyjson_obj_iter_get_val(key));
    const auto it = std::ranges::find(
        *files, result_file, &std::pair<std::string, std::string>::first);
    if (it != files->end()) {
      it->second = source;
    } else {
      files->emplace_back(result_file, source);
    }
  }
}

yyjson_mut_val* String(yyjson_mut_doc* doc, const std::string& text) {
  return yyjson_mut_strncpy(doc, text.data(), text.size());
}

yyjson_mut_val* StringList(yyjson_mut_doc* doc,
                           const std::vector<std::string>& items) {
  yyjson_mut_val* list = yyjson_mut_arr(doc);
  for (const std::string& item : items) {
    yyjson_mut_arr_append(list, String(doc, item));
  }
  return list;
}

}  // namespace

void SetResultSource(const std::string& result_file, const std::string& source,
                     MetadataTool* tool) {
  std::vector<std::pair<std::string, std::string>>& files =
      tool->result_source_files;
  const auto it = std::ranges::find(
      files, result_file, &std::pair<std::string, std::string>::first);
  if (it != files.end()) {
    it->second = source;
  } else {
    files.emplace_back(result_file, source);
  }
}

void RemoveResultSource(const std::string& result_file, MetadataTool* tool) {
  std::erase_if(tool->result_source_files, [&result_file](const auto& item) {
    return item.first == result_file;
  });
}

SourceFiles ResultSourceFiles(yyjson_val* metadata) {
  SourceFiles files;
  if (yyjson_val* files_v1 = util::JsonGet(metadata, "result_source_files")) {
    AddObject(files_v1, &files);
    return files;
  }
  yyjson_arr_iter iter =
      yyjson_arr_iter_with(util::JsonGet(metadata, "tools"));
  while (yyjson_val* tool = yyjson_arr_iter_next(&iter)) {
    AddObject(util::JsonGet(tool, "result_source_files"), &files);
  }
  return files;
}

std::optional<SourceFiles> ReadResultSourceFiles(const std::string& path) {
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  if (!document) return std::nullopt;
  return ResultSourceFiles(yyjson_doc_get_root(document->get()));
}

std::string MetadataJson(const MetadataTool& tool) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* entry = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, entry, "name", "codechecker");
  yyjson_mut_obj_add_int(doc, entry, "action_num", tool.action_num);
  yyjson_mut_obj_add_val(doc, entry, "command", StringList(doc, tool.command));
  yyjson_mut_obj_add_val(doc, entry, "version", String(doc, tool.version));
  yyjson_mut_obj_add_val(doc, entry, "working_directory",
                         String(doc, tool.working_directory));
  yyjson_mut_obj_add_val(doc, entry, "output_path",
                         String(doc, tool.output_path));
  yyjson_mut_val* files = yyjson_mut_obj(doc);
  for (const auto& [result_file, source] : tool.result_source_files) {
    yyjson_mut_obj_add(files, String(doc, result_file), String(doc, source));
  }
  yyjson_mut_obj_add_val(doc, entry, "result_source_files", files);
  yyjson_mut_val* analyzers = yyjson_mut_obj(doc);
  for (const auto& [name, metadata] : tool.analyzers) {
    yyjson_mut_val* analyzer = yyjson_mut_obj(doc);
    yyjson_mut_val* checkers = yyjson_mut_obj(doc);
    for (const auto& [checker, enabled] : metadata.checkers) {
      yyjson_mut_obj_add(checkers, String(doc, checker),
                         yyjson_mut_bool(doc, enabled));
    }
    yyjson_mut_obj_add_val(doc, analyzer, "checkers", checkers);
    const AnalyzerStatistics& stats = metadata.statistics;
    yyjson_mut_val* statistics = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, statistics, "failed", stats.failed);
    yyjson_mut_obj_add_val(doc, statistics, "failed_sources",
                           StringList(doc, stats.failed_sources));
    yyjson_mut_obj_add_int(doc, statistics, "successful", stats.successful);
    yyjson_mut_obj_add_val(doc, statistics, "successful_sources",
                           StringList(doc, stats.successful_sources));
    yyjson_mut_obj_add_val(doc, statistics, "version",
                           String(doc, stats.version));
    yyjson_mut_obj_add_val(doc, analyzer, "analyzer_statistics", statistics);
    yyjson_mut_obj_add(analyzers, String(doc, name), analyzer);
  }
  yyjson_mut_obj_add_val(doc, entry, "analyzers", analyzers);
  if (tool.run_name) {
    yyjson_mut_obj_add_val(doc, entry, "run_name", String(doc, *tool.run_name));
  }
  if (tool.skipped) {
    yyjson_mut_obj_add_int(doc, entry, "skipped", *tool.skipped);
  }
  if (tool.timestamps) {
    yyjson_mut_val* timestamps = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, timestamps, "begin", tool.timestamps->begin);
    yyjson_mut_obj_add_real(doc, timestamps, "end", tool.timestamps->end);
    yyjson_mut_obj_add_val(doc, entry, "timestamps", timestamps);
  }
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, root, "version", 2);
  yyjson_mut_val* tools = yyjson_mut_arr(doc);
  yyjson_mut_arr_append(tools, entry);
  yyjson_mut_obj_add_val(doc, root, "tools", tools);
  return util::DumpJson(root);
}

}  // namespace cc::analyzer
