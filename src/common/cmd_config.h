// Port of codechecker_common/cmd_config.py: options read from a --config
// file (config-file.md, common-infrastructure.md §5).

#ifndef CC_COMMON_CMD_CONFIG_H_
#define CC_COMMON_CMD_CONFIG_H_

#include <string>
#include <string_view>
#include <vector>

#include "util/argparse.h"
#include "util/result.h"

namespace cc::common {

// Adds --config to `parser`, in the help section `group` (Python:
// add_option()).
void AddConfigOption(util::argparse::Parser& parser, int group);

// Returns the options of the --config file for `subcommand` (Python:
// process_config_file()): none if --config was not given or the file does
// not exist. Logs the options it found.
Result<std::vector<std::string>> ProcessConfigFile(
    const util::argparse::Namespace& args, std::string_view subcommand);

// Fails with "Configuration file '<file>' does not exist." if --config was
// given with a missing file (Python: check_config_file()).
Result<void> CheckConfigFile(const util::argparse::Namespace& args);

}  // namespace cc::common

#endif  // CC_COMMON_CMD_CONFIG_H_
