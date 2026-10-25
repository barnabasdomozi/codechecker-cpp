#include "report/output/tool_info.h"

#include <yyjson.h>

#include <cstdlib>
#include <format>
#include <string>

#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

}  // namespace

ToolInfo GetToolInfo() {
  const ToolInfo report_converter = {.name = "report-converter",
                                     .version = "0.1.0"};
  const char* data_files_dir = std::getenv("CC_DATA_FILES_DIR");
  if (data_files_dir == nullptr || *data_files_dir == '\0') {
    return report_converter;
  }
  const std::string path = util::pypath::Join(
      {data_files_dir, "config", "analyzer_version.json"});
  if (!util::pypath::Exists(path)) return report_converter;
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    kLog.Warning("Failed to open json file: " + path);
    kLog.Warning(content.error().message);
    return report_converter;
  }
  const Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(content.value()));
  if (!document.ok()) {
    kLog.Warning(path + " is not a valid json file.");
    kLog.Warning(document.error().message);
    return report_converter;
  }
  yyjson_val* version =
      util::JsonGet(yyjson_doc_get_root(document.value().get()), "version");
  if (version == nullptr || yyjson_is_null(version) ||
      (yyjson_is_obj(version) && yyjson_obj_size(version) == 0)) {
    return report_converter;
  }
  return {.name = "CodeChecker",
          .version = std::format(
              "{}.{}.{}", util::JsonToStr(util::JsonGet(version, "major")),
              util::JsonToStr(util::JsonGet(version, "minor")),
              util::JsonToStr(util::JsonGet(version, "revision")))};
}

}  // namespace cc::report
