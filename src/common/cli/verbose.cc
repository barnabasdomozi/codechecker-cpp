#include "common/cli/verbose.h"

#include <optional>
#include <string_view>

#include "util/argparse.h"

namespace cc::common::cli {

void AddVerboseArgument(util::argparse::Parser& parser) {
  parser.AddArgument({.flags = {"--verbose"},
                      .dest = "verbose",
                      .choices = {"info", "debug_analyzer", "debug"},
                      .default_value = std::nullopt,
                      .help = "Set verbosity level."});
}

std::optional<std::string_view> Verbose(const util::argparse::Namespace& args) {
  if (!args.Has("verbose")) return std::nullopt;
  return args.Get("verbose");
}

}  // namespace cc::common::cli
