// zlib streams, the format in which the server keeps source files, bug
// paths, blame information and analyzer outputs in its databases
// (libraries.md, "Compression: zlib").

#ifndef CC_UTIL_COMPRESSION_H_
#define CC_UTIL_COMPRESSION_H_

#include <stddef.h>

#include <string>
#include <string_view>

#include "util/result.h"

namespace cc::util {

// Returns the data of the zlib stream `data`; bytes after the end of the
// stream are ignored (Python: zlib.decompress(data)). The error message is
// the text of Python's zlib.error, for example "Error -3 while decompressing
// data: incorrect header check".
Result<std::string> ZlibDecompress(std::string_view data);

// The first part of the data of a zlib stream.
struct ZlibPart {
  std::string data;
  // Whether input is left over because the data reached the limit (Python:
  // the unconsumed_tail of the decompressor is not empty).
  bool limited = false;
};

// Returns at most the first `max_length` (> 0) bytes of the data of the zlib
// stream `data` (Python: zlib.decompressobj().decompress(data, max_length)).
// Unlike ZlibDecompress(), a stream that is cut off is not an error: the
// data before the cut is returned. The error message is the one of
// ZlibDecompress().
Result<ZlibPart> ZlibDecompressPart(std::string_view data, size_t max_length);

// Returns `data` as a zlib stream compressed at the best level (Python:
// zlib.compress(data, zlib.Z_BEST_COMPRESSION)).
std::string ZlibCompress(std::string_view data);

// Replaces the content of the file `path` with its zlib stream (see
// ZlibCompress()) and returns that stream. The error message is the one of
// ReadFile() or WriteFile().
Result<std::string> ZlibCompressFile(const std::string& path);

}  // namespace cc::util

#endif  // CC_UTIL_COMPRESSION_H_
