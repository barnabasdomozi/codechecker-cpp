// Port of the helpers of codechecker_common/util.py.

#ifndef CC_COMMON_UTIL_H_
#define CC_COMMON_UTIL_H_

#include <optional>
#include <string>

#include "util/json.h"

namespace cc::common {

// Returns the JSON document in the file at `path`, read like Python's
// load_json(path): invalid UTF-8 is dropped. Returns nullopt, after logging
// warnings that say why, if the file cannot be read or is not valid JSON.
std::optional<util::JsonDocument> LoadJson(const std::string& path);

}  // namespace cc::common

#endif  // CC_COMMON_UTIL_H_
