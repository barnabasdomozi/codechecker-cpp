// The subcommands of codechecker-cpp (architecture.md §5.3). The web side's
// subcommands come from web_subcommands.cc, or from no_web_subcommands.cc in
// a build without the web side (CC_BUILD_WEB=OFF).

#ifndef CC_LAUNCHER_SUBCOMMANDS_H_
#define CC_LAUNCHER_SUBCOMMANDS_H_

#include <span>
#include <string_view>

#include "common/cli/version.h"
#include "util/argparse.h"

namespace cc::launcher {

// One subcommand: its parser and its entry point.
struct Subcommand {
  std::string_view name;  // For example "analyze".
  std::string_view help;  // One-line help for the top-level list.
  void (*add_arguments)(util::argparse::Parser& parser);
  int (*run)(const util::argparse::Namespace& args);  // Returns the exit code.
};

// Returns the subcommands of the analyzer side, `version` included, in the
// order of the top-level help.
std::span<const Subcommand> AnalyzerSubcommands();

// Returns the subcommands of the web side, which follow those of the
// analyzer side; empty without the web side.
std::span<const Subcommand> WebSubcommands();

// Returns the reader of the web part's version for `version`; null without
// the web side.
common::cli::VersionReader WebVersionReader();

}  // namespace cc::launcher

#endif  // CC_LAUNCHER_SUBCOMMANDS_H_
