#include "web/cli/web_version.h"

#include <format>
#include <string>
#include <utility>

#include "api/api_version.h"
#include "common/cli/version.h"
#include "common/package.h"
#include "common/version.h"
#include "util/argparse.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::web::cli {

void AddWebVersionArguments(util::argparse::Parser& parser) {
  // The prog of the reference, which is not the subcommand's name.
  parser.set_prog(std::format("{} webserver version", common::kProgramName));
  common::cli::SetUpVersionParser(parser, kWebVersionHelp);
}

int RunWebVersion(const util::argparse::Namespace& args) {
  return common::cli::RunPartVersion(args, "web", &ReadWebVersion);
}

Result<common::VersionInfo> ReadWebVersion() {
  Result<common::VersionInfo> info = common::ReadVersionFile(util::pypath::Join(
      {common::DataFilesDir(), "config", "web_version.json"}));
  if (!info.ok()) return info;
  common::VersionInfo web = std::move(info).value();
  web.api_version = std::string(api::kApiVersion);
  return Result<common::VersionInfo>(std::move(web));
}

}  // namespace cc::web::cli
