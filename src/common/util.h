// Port of the helpers of codechecker_common/util.py.

#ifndef CC_COMMON_UTIL_H_
#define CC_COMMON_UTIL_H_

#include <optional>
#include <string>
#include <vector>

#include "util/json.h"
#include "util/result.h"

namespace cc::common {

// Returns the JSON document in the file at `path`, read like Python's
// load_json(path): invalid UTF-8 is dropped. Returns nullopt, after logging
// warnings that say why unless `display_warning` is false, if the file
// cannot be read or is not valid JSON.
std::optional<util::JsonDocument> LoadJson(const std::string& path,
                                           bool display_warning = true);

// Returns the YAML document in the file at `path`, read like Python's
// load_yaml(path). Returns nullopt, after logging warnings that say why, if
// the file cannot be read or is not valid YAML.
std::optional<util::JsonDocument> LoadYaml(const std::string& path);

// Returns the command line arguments in the file at `path` (Python:
// load_args_from_file()): the stripped content with every "$(NAME)" replaced
// by the environment variable NAME (unset ones are logged and replaced by
// ""), split with shell rules. A missing file fails with the message
// "File not found: <path>".
Result<std::vector<std::string>> LoadArgsFromFile(const std::string& path);

// Returns where the absolute path `full_path` is in the folder `root_path`,
// which stands for "/": "/a/b" in "/tmp/root" is "/tmp/root/a/b" (Python:
// path_for_fake_root()).
std::string PathForFakeRoot(const std::string& full_path,
                            const std::string& root_path);

}  // namespace cc::common

#endif  // CC_COMMON_UTIL_H_
