// The web side's part of the subcommand table, in a build with the web side.

#include "launcher/subcommands.h"

#include <span>

#include "common/cli/version.h"
#include "web/cli/web_version.h"

namespace cc::launcher {
namespace {

constexpr Subcommand kWebSubcommands[] = {
    {"web-version", web::cli::kWebVersionHelp,
     &web::cli::AddWebVersionArguments, &web::cli::RunWebVersion},
};

}  // namespace

std::span<const Subcommand> WebSubcommands() { return kWebSubcommands; }

common::cli::VersionReader WebVersionReader() {
  return &web::cli::ReadWebVersion;
}

}  // namespace cc::launcher
