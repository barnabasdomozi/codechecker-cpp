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

// Returns the command line arguments in the file at `path` (Python:
// load_args_from_file()): the stripped content with every "$(NAME)" replaced
// by the environment variable NAME (unset ones are logged and replaced by
// ""), split with shell rules. A missing file fails with the message
// "File not found: <path>".
Result<std::vector<std::string>> LoadArgsFromFile(const std::string& path);

}  // namespace cc::common

#endif  // CC_COMMON_UTIL_H_
