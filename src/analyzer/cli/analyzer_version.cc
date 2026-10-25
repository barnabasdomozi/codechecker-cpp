#include "analyzer/cli/analyzer_version.h"

#include <format>

#include "common/cli/version.h"
#include "common/package.h"
#include "common/version.h"
#include "util/argparse.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::analyzer::cli {

void AddAnalyzerVersionArguments(util::argparse::Parser& parser) {
  // The prog of the reference, which is not the subcommand's name.
  parser.set_prog(std::format("{} analyzer version", common::kProgramName));
  common::cli::SetUpVersionParser(parser, kAnalyzerVersionHelp);
}

int RunAnalyzerVersion(const util::argparse::Namespace& args) {
  return common::cli::RunPartVersion(args, "analyzer", &ReadAnalyzerVersion);
}

Result<common::VersionInfo> ReadAnalyzerVersion() {
  return common::ReadVersionFile(util::pypath::Join(
      {common::DataFilesDir(), "config", "analyzer_version.json"}));
}

}  // namespace cc::analyzer::cli
