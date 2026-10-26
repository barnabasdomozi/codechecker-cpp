#include "common/cmd_config.h"

#include <yyjson.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/util.h"
#include "util/argparse.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/yaml.h"

namespace cc::common {
namespace {

constexpr util::Logger kLog("system");

// Returns the items of the section `name` of `config` (Python:
// cfg.get(name, [])).
std::vector<std::string> Section(yyjson_val* config, std::string_view name) {
  std::vector<std::string> options;
  yyjson_arr_iter iter = yyjson_arr_iter_with(util::JsonGet(config, name));
  while (yyjson_val* option = yyjson_arr_iter_next(&iter)) {
    options.push_back(util::JsonToStr(option));
  }
  return options;
}

// Python: get_analyze_options().
std::vector<std::string> AnalyzeOptions(yyjson_val* config) {
  std::vector<std::string> analyze = Section(config, "analyze");
  std::vector<std::string> analyzer = Section(config, "analyzer");
  if (analyze.empty()) return analyzer;
  if (!analyzer.empty()) {
    kLog.Warning(
        "There is an 'analyze' and an 'analyzer' config configuration option "
        "in the config file. Please use the 'analyze' value to be in sync "
        "with the subcommands.\nUsing the 'analyze' configuration.");
  }
  return analyze;
}

}  // namespace

void AddConfigOption(util::argparse::Parser& parser, int group) {
  parser.AddArgument(
      {.flags = {"--config"},
       .dest = "config_file",
       .default_none = true,
       .help = "R|Allow the configuration from an explicit configuration "
               "file. The values configured in the config file will "
               "overwrite the values set in the command line.\nYou can use "
               "any environment variable inside this file and it will be "
               "expaneded.\nFor more information see the docs: "
               "https://github.com/Ericsson/codechecker/tree/master/docs/"
               "config_file.md",
       .group = group});
}

Result<std::vector<std::string>> ProcessConfigFile(
    const util::argparse::Namespace& args, std::string_view subcommand) {
  using Options = Result<std::vector<std::string>>;
  if (!args.Has("config_file")) return Options(std::vector<std::string>());
  const std::string& config_file = args.Get("config_file");
  if (config_file.empty() || !util::pypath::Exists(config_file)) {
    return Options(std::vector<std::string>());
  }
  std::optional<util::JsonDocument> document;
  if (config_file.ends_with(".yaml") || config_file.ends_with(".yml")) {
    const Result<std::string> content = util::ReadFile(config_file);
    if (!content.ok()) return Options(content.error());
    Result<util::JsonDocument> yaml = util::LoadYamlStrings(
        util::DecodeUtf8Ignore(content.value()), config_file);
    if (!yaml.ok()) return Options(yaml.error());
    document = std::move(yaml).value();
  } else {
    document = LoadJson(config_file);
  }
  yyjson_val* config =
      document ? yyjson_doc_get_root(document->get()) : nullptr;
  if (document && !yyjson_is_obj(config)) {
    // Python fails calling get() on it.
    return Options(Error{.message = "AttributeError: the config file is not "
                                    "a mapping"});
  }
  std::vector<std::string> options;
  if (subcommand == "analyze") {
    options = AnalyzeOptions(config);
  } else if (subcommand == "check") {
    options = AnalyzeOptions(config);
    for (std::string& option : Section(config, "parse")) {
      options.push_back(std::move(option));
    }
  } else {
    options = Section(config, subcommand);
  }
  if (!options.empty()) {
    kLog.Info(std::format(
        "Extending command line options with {} options from '{}' file: {}",
        subcommand, config_file, util::Join(options, " ")));
  }
  return Options(std::move(options));
}

Result<void> CheckConfigFile(const util::argparse::Namespace& args) {
  if (!args.Has("config_file")) return Result<void>();
  const std::string& config_file = args.Get("config_file");
  if (!config_file.empty() && !util::pypath::Exists(config_file)) {
    return Result<void>(Error{.message = std::format(
                                  "Configuration file '{}' does not exist.",
                                  config_file)});
  }
  return Result<void>();
}

}  // namespace cc::common
