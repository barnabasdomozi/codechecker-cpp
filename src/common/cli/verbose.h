// The --verbose option that every subcommand has (Python:
// logger.add_verbose_arguments(); common-infrastructure.md §6).

#ifndef CC_COMMON_CLI_VERBOSE_H_
#define CC_COMMON_CLI_VERBOSE_H_

#include <optional>
#include <string_view>

#include "util/argparse.h"

namespace cc::common::cli {

// Adds --verbose {info,debug_analyzer,debug} to `parser`.
void AddVerboseArgument(util::argparse::Parser& parser);

// Returns the value of --verbose, or nullopt if it was not given.
std::optional<std::string_view> Verbose(const util::argparse::Namespace& args);

}  // namespace cc::common::cli

#endif  // CC_COMMON_CLI_VERBOSE_H_
