// zlib streams, the format in which the server keeps source files, bug
// paths, blame information and analyzer outputs in its databases
// (libraries.md, "Compression: zlib").

#ifndef CC_UTIL_COMPRESSION_H_
#define CC_UTIL_COMPRESSION_H_

#include <string>
#include <string_view>

#include "util/result.h"

namespace cc::util {

// Returns the data of the zlib stream `data`; bytes after the end of the
// stream are ignored (Python: zlib.decompress(data)). The error message is
// the text of Python's zlib.error, for example "Error -3 while decompressing
// data: incorrect header check".
Result<std::string> ZlibDecompress(std::string_view data);

}  // namespace cc::util

#endif  // CC_UTIL_COMPRESSION_H_
