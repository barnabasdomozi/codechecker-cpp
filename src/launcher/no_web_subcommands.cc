// The web side's part of the subcommand table, in a build without the web
// side (CC_BUILD_WEB=OFF): no subcommands, like the Python analyzer-only
// package.

#include "launcher/subcommands.h"

#include <span>

#include "common/cli/version.h"

namespace cc::launcher {

std::span<const Subcommand> WebSubcommands() { return {}; }

common::cli::VersionReader WebVersionReader() { return nullptr; }

}  // namespace cc::launcher
