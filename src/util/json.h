// JSON documents: read with yyjson, written like Python's json module
// (libraries.md, "JSON"). Values are read and built with yyjson's own API
// (yyjson_obj_get(), yyjson_mut_obj_add_strncpy(), ...).

#ifndef CC_UTIL_JSON_H_
#define CC_UTIL_JSON_H_

#include <yyjson.h>

#include <memory>
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

}  // namespace cc::util

#endif  // CC_UTIL_JSON_H_
