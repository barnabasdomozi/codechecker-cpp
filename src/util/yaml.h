// YAML documents read like PyYAML's yaml.safe_load() and written like
// yaml.dump(), with libyaml (libraries.md, "YAML").
//
// The values become a yyjson document, like json.loads() gives: mappings are
// objects, sequences arrays, and plain scalars get PyYAML's YAML 1.1 types
// (null, booleans such as "yes" and "off", integers such as "0x1F", "0777"
// and "1:30", floats). Timestamps stay strings. Mapping keys that are not
// strings become their Python str(): 1 becomes "1", True becomes "True".

#ifndef CC_UTIL_YAML_H_
#define CC_UTIL_YAML_H_

#include <yyjson.h>

#include <string>
#include <string_view>

#include "util/json.h"
#include "util/result.h"

namespace cc::util {

// Parses the YAML document `text`, read from the file `name` (for error
// messages). An empty stream gives a null root. More than one document is
// an error, as in PyYAML.
Result<JsonDocument> SafeLoadYaml(std::string_view text, std::string_view name);

// Parses the YAML document `text` like yaml.load(text, Loader=BaseLoader):
// every scalar is a string.
Result<JsonDocument> LoadYamlStrings(std::string_view text,
                                     std::string_view name);

// Returns `value` as yaml.dump(value, width=width) writes it: block style,
// mapping keys sorted, strings quoted where PyYAML quotes them and non-ASCII
// characters escaped.
std::string DumpYaml(yyjson_mut_val* value, int width);

}  // namespace cc::util

#endif  // CC_UTIL_YAML_H_
