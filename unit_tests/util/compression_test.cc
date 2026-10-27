#include "util/compression.h"

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "util/result.h"

namespace cc::util {
namespace {

// Python: zlib.compress(b'hello zlib').
constexpr std::string_view kCompressed(
    "\x78\x9c\xcb\x48\xcd\xc9\xc9\x57\xa8\xca\xc9\x4c\x02\x00\x15\x95\x03"
    "\xe6",
    18);

TEST(CompressionTest, Decompresses) {
  const Result<std::string> data = ZlibDecompress(kCompressed);
  ASSERT_TRUE(data.ok());
  EXPECT_EQ(data.value(), "hello zlib");
  // Bytes after the end of the stream are ignored.
  EXPECT_EQ(ZlibDecompress(std::string(kCompressed) + "trailing").value(),
            "hello zlib");
}

TEST(CompressionTest, ErrorsHavePythonsMessages) {
  EXPECT_EQ(ZlibDecompress("not zlib").error().message,
            "Error -3 while decompressing data: incorrect header check");
  EXPECT_EQ(ZlibDecompress(kCompressed.substr(0, 15)).error().message,
            "Error -5 while decompressing data: incomplete or truncated "
            "stream");
}

}  // namespace
}  // namespace cc::util
