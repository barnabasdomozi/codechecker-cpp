#include "launcher/subcommands.h"

#include <span>

#include "analyzer/cli/analyze.h"
#include "analyzer/cli/analyzer_version.h"
#include "analyzer/cli/check.h"
#include "analyzer/cli/log.h"
#include "analyzer/cli/parse.h"
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
    {"analyze", analyzer::cli::kAnalyzeHelp,
     &analyzer::cli::AddAnalyzeArguments, &analyzer::cli::RunAnalyze,
     /*config_file=*/true},
    {"analyzer-version", analyzer::cli::kAnalyzerVersionHelp,
     &analyzer::cli::AddAnalyzerVersionArguments,
     &analyzer::cli::RunAnalyzerVersion},
    {"check", analyzer::cli::kCheckHelp, &analyzer::cli::AddCheckArguments,
     &analyzer::cli::RunCheck, /*config_file=*/true},
    {"log", analyzer::cli::kLogHelp, &analyzer::cli::AddLogArguments,
     &analyzer::cli::RunLog},
    {"parse", analyzer::cli::kParseHelp, &analyzer::cli::AddParseArguments,
     &analyzer::cli::RunParse, /*config_file=*/true},
};

}  // namespace

std::span<const Subcommand> AnalyzerSubcommands() {
  return kAnalyzerSubcommands;
}

}  // namespace cc::launcher
