// The web side's part of the subcommand table, in a build with the web side.

#include "launcher/subcommands.h"

#include <span>

#include "common/cli/version.h"
#include "server/cli/server.h"
#include "web/cli/web_version.h"

namespace cc::launcher {
namespace {

constexpr Subcommand kWebSubcommands[] = {
    {"web-version", web::cli::kWebVersionHelp,
     &web::cli::AddWebVersionArguments, &web::cli::RunWebVersion},
    {"server", server::cli::kServerHelp, &server::cli::AddServerArguments,
     &server::cli::RunServer, /*config_file=*/true},
};

}  // namespace

std::span<const Subcommand> WebSubcommands() { return kWebSubcommands; }

common::cli::VersionReader WebVersionReader() {
  return &web::cli::ReadWebVersion;
}

}  // namespace cc::launcher
