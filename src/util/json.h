// JSON documents: read with yyjson, written like Python's json module
// (libraries.md, "JSON"). Values are read and built with yyjson's own API
// (yyjson_obj_get(), yyjson_mut_obj_add_strncpy(), ...).

#ifndef CC_UTIL_JSON_H_
#define CC_UTIL_JSON_H_

#include <stdint.h>
#include <yyjson.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "util/result.h"

namespace cc::util {

// Frees yyjson documents.
struct JsonDocumentDeleter {
  void operator()(yyjson_doc* document) const { yyjson_doc_free(document); }
  void operator()(yyjson_mut_doc* document) const {
    yyjson_mut_doc_free(document);
  }
};

// An owned, read-only document.
using JsonDocument = std::unique_ptr<yyjson_doc, JsonDocumentDeleter>;
// An owned document that is being built.
using MutableJsonDocument =
    std::unique_ptr<yyjson_mut_doc, JsonDocumentDeleter>;

// Parses `text` like Python's json.loads(): NaN and Infinity are accepted,
// and integers too big for 64 bits are kept as raw text. On failure, the
// error message has the shape of json's:
// "<problem>: line 1 column 5 (char 4)".
Result<JsonDocument> ParseJson(std::string_view text);

// Returns a new, empty document to build values in.
MutableJsonDocument NewJsonDocument();

// Returns `value` as Python's json.dumps() writes it with its defaults:
// ", " and ": " separators, and non-ASCII characters as \uXXXX escapes.
std::string DumpJson(yyjson_mut_val* value);

// Returns `value` as Python's json.dumps(value, indent=indent) writes it:
// each item on its own line, indented by `indent` spaces per level.
std::string DumpJsonIndent(yyjson_mut_val* value, int indent);

// Returns the text of the string `value`; "" if `value` is not a string.
std::string JsonString(yyjson_val* value);
std::string JsonString(yyjson_mut_val* value);

// Returns the member `key` of the object `value`, or null if `value` is not
// an object or has no such member. Of duplicate members the last one
// counts, as in json.loads(). Lookups are linear.
yyjson_val* JsonGet(yyjson_val* value, std::string_view key);

// Returns Python's bool(value): false for a missing value, null, false, 0,
// "", [] and {}.
bool JsonTruthy(yyjson_val* value);

// Returns `value` as Python's int(value) converts it: integers, floats
// (truncated), booleans and strings that int() accepts; nullopt for the
// others.
std::optional<int64_t> JsonToInt(yyjson_val* value);

// Returns `value` as Python's str(value) writes it: strings as they are,
// numbers like repr(), true as "True", null as "None", and arrays and
// objects as JSON.
std::string JsonToStr(yyjson_val* value);

// Returns `value` as Python's repr() writes the value json.loads() or
// yaml.safe_load() gives: {'a': [1, 'b', None, True]}.
std::string JsonToRepr(yyjson_val* value);

// Returns the member `key` of the object `value` (Python: value[key]), or
// an error with the text of the exception Python raises: "KeyError: 'key'"
// if it is missing, a TypeError if `value` is not an object.
Result<yyjson_val*> JsonSubscript(yyjson_val* value, std::string_view key);

// Returns int(value[key]) (see JsonToInt()), or an error with the text of
// Python's exception.
Result<int64_t> JsonSubscriptInt(yyjson_val* value, std::string_view key);


}  // namespace cc::util

#endif  // CC_UTIL_JSON_H_
