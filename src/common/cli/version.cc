#include "common/cli/version.h"

#include <yyjson.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/cli/verbose.h"
#include "common/version.h"
#include "util/argparse.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/result.h"
#include "util/twodim.h"

namespace cc::common::cli {
namespace {

constexpr util::Logger kLog("system");

// Python: Version.to_list(). nullopt values are printed as empty cells.
std::vector<std::vector<std::string>> VersionRows(const VersionInfo& info) {
  std::vector<std::vector<std::string>> rows = {
      {"Base package version", info.version},
      {"Package build date", info.build_date},
      {"Git commit ID (hash)", info.git_hash.value_or("")},
      {"Git tag information", info.git_tag.value_or("")}};
  if (info.api_version) {
    rows.push_back({"Server supported Thrift API version", *info.api_version});
    rows.push_back({"Client Thrift API version", *info.api_version});
  }
  return rows;
}

yyjson_mut_val* StringOrNull(yyjson_mut_doc* doc,
                             const std::optional<std::string>& value) {
  return value ? yyjson_mut_strncpy(doc, value->data(), value->size())
               : yyjson_mut_null(doc);
}

// Python: Version.to_dict().
yyjson_mut_val* VersionJson(yyjson_mut_doc* doc, const VersionInfo& info) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_strncpy(doc, object, "base_package_version",
                             info.version.data(), info.version.size());
  yyjson_mut_obj_add_strncpy(doc, object, "package_build_date",
                             info.build_date.data(), info.build_date.size());
  yyjson_mut_obj_add_val(doc, object, "git_commit",
                         StringOrNull(doc, info.git_hash));
  yyjson_mut_obj_add_val(doc, object, "git_tag",
                         StringOrNull(doc, info.git_tag));
  if (info.api_version) {
    yyjson_mut_val* server_versions = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_strncpy(doc, server_versions, info.api_version->data(),
                               info.api_version->size());
    yyjson_mut_obj_add_val(doc, object, "server_api_version",
                           server_versions);
    yyjson_mut_obj_add_val(doc, object, "client_api_version",
                           StringOrNull(doc, info.api_version));
  }
  return object;
}

// Prints the version of one part in `format` (Python: Version.print()).
void PrintVersion(std::string_view part, const VersionInfo& info,
                  const std::string& format) {
  if (format == "json") {
    const util::MutableJsonDocument document = util::NewJsonDocument();
    util::Print(util::DumpJson(VersionJson(document.get(), info)));
    return;
  }
  kLog.Info(std::format("CodeChecker {} version:", part));
  util::Print(
      util::twodim::ToString(format, {"Kind", "Version"}, VersionRows(info)));
  util::Flush(util::Stream::kStdout);
  if (info.version.find("rc") != std::string::npos) {
    kLog.Warning(
        "This version is only a release candidate! If you encounter any "
        "problems, please submit a bug report!");
  }
}

}  // namespace

void SetUpVersionParser(util::argparse::Parser& parser,
                        std::string_view description) {
  parser.set_description(std::string(description));
  parser.set_help_format(util::argparse::HelpFormat::kArgumentDefaults);
  parser.set_allow_abbrev(false);
  parser.AddArgument({.flags = {"-o", "--output"},
                      .dest = "output_format",
                      .choices = {"rows", "table", "csv", "json"},
                      .default_value = "table",
                      .help = "The format to use when printing the version."});
  AddVerboseArgument(parser);
}

void AddVersionArguments(util::argparse::Parser& parser) {
  SetUpVersionParser(parser, kVersionHelp);
}

int RunVersion(const util::argparse::Namespace& args,
               VersionReader read_analyzer, VersionReader read_web) {
  util::SetupLogger(Verbose(args), util::Stream::kStdout);
  const Result<VersionInfo> analyzer = read_analyzer();
  if (!analyzer.ok()) return analyzer.error().exit_code;
  std::optional<VersionInfo> web;
  if (read_web != nullptr) {
    Result<VersionInfo> result = read_web();
    if (!result.ok()) return result.error().exit_code;
    web = std::move(result).value();
  }
  const std::string& format = args.Get("output_format");
  if (format == "json") {
    const util::MutableJsonDocument document = util::NewJsonDocument();
    yyjson_mut_doc* doc = document.get();
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, object, "analyzer",
                           VersionJson(doc, analyzer.value()));
    yyjson_mut_obj_add_val(doc, object, "web",
                           web ? VersionJson(doc, *web) : yyjson_mut_null(doc));
    util::Print(util::DumpJson(object));
    return 0;
  }
  PrintVersion("analyzer", analyzer.value(), format);
  util::Print("");
  if (web) PrintVersion("web", *web, format);
  return 0;
}

int RunPartVersion(const util::argparse::Namespace& args,
                   std::string_view part, VersionReader read) {
  const std::string& format = args.Get("output_format");
  // Only the table format logs to stdout, so the others can be parsed.
  util::SetupLogger(Verbose(args), format == "table" ? util::Stream::kStdout
                                                     : util::Stream::kStderr);
  const Result<VersionInfo> info = read();
  if (!info.ok()) return info.error().exit_code;
  PrintVersion(part, info.value(), format);
  return 0;
}

}  // namespace cc::common::cli
