#include "util/compression.h"

#include <stddef.h>
#include <zlib.h>

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "util/result.h"

namespace cc::util {
namespace {

// Returns the error of the zlib call that returned `code` on `stream`
// (Python: zlib_error()).
Error ZlibError(const z_stream& stream, int code) {
  std::string_view message;
  if (code == Z_VERSION_ERROR) {
    message = "library version mismatch";
  } else if (stream.msg != nullptr) {
    message = stream.msg;
  } else if (code == Z_BUF_ERROR) {
    message = "incomplete or truncated stream";
  } else if (code == Z_STREAM_ERROR) {
    message = "inconsistent stream state";
  } else if (code == Z_DATA_ERROR) {
    message = "invalid input data";
  }
  return Error{.message = std::format("Error {} while decompressing data: {}",
                                      code, message)};
}

}  // namespace

Result<std::string> ZlibDecompress(std::string_view data) {
  z_stream stream = {};
  int code = inflateInit(&stream);
  if (code != Z_OK) return Result<std::string>(ZlibError(stream, code));
  // zlib does not change the input; its API predates const.
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  // The part of the input not given to zlib yet: avail_in is a uInt, so
  // input above its range is given in parts.
  size_t remaining = data.size();
  std::string output;
  char buffer[16384];
  do {
    if (stream.avail_in == 0 && remaining > 0) {
      const size_t part = std::min<size_t>(
          remaining, std::numeric_limits<uInt>::max());
      stream.avail_in = static_cast<uInt>(part);
      remaining -= part;
    }
    stream.next_out = reinterpret_cast<Bytef*>(buffer);
    stream.avail_out = sizeof(buffer);
    code = inflate(&stream, Z_NO_FLUSH);
    output.append(buffer, sizeof(buffer) - stream.avail_out);
  } while (code == Z_OK);
  // Z_BUF_ERROR: the input ended before the stream did.
  if (code != Z_STREAM_END) {
    Error error = ZlibError(stream, code);
    inflateEnd(&stream);
    return Result<std::string>(std::move(error));
  }
  inflateEnd(&stream);
  return Result<std::string>(std::move(output));
}

}  // namespace cc::util
