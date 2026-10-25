#include "launcher/subcommands.h"

#include <span>

#include "analyzer/cli/analyzer_version.h"
#include "common/cli/version.h"
#include "util/argparse.h"

namespace cc::launcher {
namespace {

// `version` prints every part of the package that this build has.
int RunVersion(const util::argparse::Namespace& args) {
  return common::cli::RunVersion(args, &analyzer::cli::ReadAnalyzerVersion,
                                 WebVersionReader());
}

constexpr Subcommand kAnalyzerSubcommands[] = {
    {"version", common::cli::kVersionHelp, &common::cli::AddVersionArguments,
     &RunVersion},
    {"analyzer-version", analyzer::cli::kAnalyzerVersionHelp,
     &analyzer::cli::AddAnalyzerVersionArguments,
     &analyzer::cli::RunAnalyzerVersion},
};

}  // namespace

std::span<const Subcommand> AnalyzerSubcommands() {
  return kAnalyzerSubcommands;
}

}  // namespace cc::launcher
