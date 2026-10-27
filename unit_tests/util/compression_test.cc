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

TEST(CompressionTest, DecompressesAPart) {
  Result<ZlibPart> part = ZlibDecompressPart(kCompressed, 5);
  ASSERT_TRUE(part.ok());
  EXPECT_EQ(part.value().data, "hello");
  EXPECT_TRUE(part.value().limited);
  part = ZlibDecompressPart(kCompressed, 10);
  EXPECT_EQ(part.value().data, "hello zlib");
  EXPECT_FALSE(part.value().limited);
  // A stream that is cut off gives what it has.
  part = ZlibDecompressPart(kCompressed.substr(0, 15), 100);
  ASSERT_TRUE(part.ok());
  EXPECT_FALSE(part.value().limited);
  EXPECT_EQ(ZlibDecompressPart("not zlib", 100).error().message,
            "Error -3 while decompressing data: incorrect header check");
}

TEST(CompressionTest, CompressesAtTheBestLevel) {
  // Python: zlib.compress(b'hello zlib', 9).
  EXPECT_EQ(ZlibCompress("hello zlib"),
            std::string("\x78\xda\xcb\x48\xcd\xc9\xc9\x57\xa8\xca\xc9"
                        "\x4c\x02\x00\x15\x95\x03\xe6",
                        18));
  EXPECT_EQ(ZlibDecompress(ZlibCompress("")).value(), "");
}

}  // namespace
}  // namespace cc::util
