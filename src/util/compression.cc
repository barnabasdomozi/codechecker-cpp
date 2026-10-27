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

// What Inflate() did.
struct Inflated {
  std::string data;
  bool ended = false;       // Whether the end of the stream was reached.
  bool input_left = false;  // Whether input was left when it stopped.
};

// Inflates the zlib stream `data` until the stream ends, the input runs out
// or `max_length` (> 0) bytes are written. A stream that is cut off is not
// an error.
Result<Inflated> Inflate(std::string_view data, size_t max_length) {
  z_stream stream = {};
  int code = inflateInit(&stream);
  if (code != Z_OK) return Result<Inflated>(ZlibError(stream, code));
  // zlib does not change the input; its API predates const.
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  // The part of the input not given to zlib yet: avail_in is a uInt, so
  // input above its range is given in parts.
  size_t remaining = data.size();
  Inflated result;
  char buffer[16384];
  do {
    if (stream.avail_in == 0 && remaining > 0) {
      const size_t part = std::min<size_t>(
          remaining, std::numeric_limits<uInt>::max());
      stream.avail_in = static_cast<uInt>(part);
      remaining -= part;
    }
    const size_t room =
        std::min(sizeof(buffer), max_length - result.data.size());
    stream.next_out = reinterpret_cast<Bytef*>(buffer);
    stream.avail_out = static_cast<uInt>(room);
    code = inflate(&stream, Z_NO_FLUSH);
    result.data.append(buffer, room - stream.avail_out);
  } while (code == Z_OK && result.data.size() < max_length);
  result.ended = code == Z_STREAM_END;
  result.input_left = stream.avail_in > 0 || remaining > 0;
  // Z_BUF_ERROR: the input ended before the stream did.
  if (code != Z_OK && code != Z_STREAM_END && code != Z_BUF_ERROR) {
    // The message points into the state that inflateEnd() frees.
    Error error = ZlibError(stream, code);
    inflateEnd(&stream);
    return Result<Inflated>(std::move(error));
  }
  inflateEnd(&stream);
  return Result<Inflated>(std::move(result));
}

}  // namespace

Result<std::string> ZlibDecompress(std::string_view data) {
  Result<Inflated> inflated = Inflate(data, std::numeric_limits<size_t>::max());
  if (!inflated.ok()) return Result<std::string>(inflated.error());
  if (!inflated.value().ended) {
    return Result<std::string>(ZlibError(z_stream{}, Z_BUF_ERROR));
  }
  return Result<std::string>(std::move(inflated.value().data));
}

Result<ZlibPart> ZlibDecompressPart(std::string_view data, size_t max_length) {
  Result<Inflated> inflated = Inflate(data, max_length);
  if (!inflated.ok()) return Result<ZlibPart>(inflated.error());
  const bool limited =
      !inflated.value().ended && inflated.value().input_left;
  return Result<ZlibPart>(
      ZlibPart{.data = std::move(inflated.value().data), .limited = limited});
}

std::string ZlibCompress(std::string_view data) {
  uLongf size = compressBound(static_cast<uLong>(data.size()));
  std::string output(size, '\0');
  // compress2() fails only without memory or with a bad level.
  (void)compress2(reinterpret_cast<Bytef*>(output.data()), &size,
                  reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uLong>(data.size()), Z_BEST_COMPRESSION);
  output.resize(size);
  return output;
}

}  // namespace cc::util
