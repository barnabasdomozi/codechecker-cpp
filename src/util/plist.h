// Property lists in the XML format, read like report-converter reads them
// (plistlib's parser on top of lxml) and written like plistlib.dump()
// (libraries.md, "XML and plist").
//
// Values are yyjson values: <dict> is an object, <array> an array,
// <string>, <integer>, <real>, <true/> and <false/> the matching scalars.
// <date> and <data> are read as strings holding their text.

#ifndef CC_UTIL_PLIST_H_
#define CC_UTIL_PLIST_H_

#include <yyjson.h>

#include <string>
#include <string_view>

#include "util/json.h"
#include "util/result.h"

namespace cc::util {

// Parses the plist document `xml`. `file_name` is the name of the file it
// was read from, for error messages. An XML syntax error has lxml's message,
// for example "Document is empty, line 1, column 1 (a.plist, line 1)".
Result<JsonDocument> ParsePlist(std::string_view xml,
                                std::string_view file_name);

// Returns `value` in plistlib.dump()'s layout: the XML declaration, Apple's
// DOCTYPE, one element per line indented with tabs, and dictionary keys
// sorted. As plistlib, it fails for null values and for strings that
// contain control characters other than tab and line breaks.
Result<std::string> DumpPlist(yyjson_mut_val* value);

}  // namespace cc::util

#endif  // CC_UTIL_PLIST_H_
