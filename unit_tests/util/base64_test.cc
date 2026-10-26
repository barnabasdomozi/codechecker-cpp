#include "util/base64.h"

#include <optional>
#include <string>

#include "gtest/gtest.h"

namespace cc::util {
namespace {

TEST(Base64Test, Encode) {
  EXPECT_EQ(Base64Encode(""), "");
  EXPECT_EQ(Base64Encode("a"), "YQ==");
  EXPECT_EQ(Base64Encode("ab"), "YWI=");
  EXPECT_EQ(Base64Encode("abc"), "YWJj");
  EXPECT_EQ(Base64Encode(std::string("\xff\xfe\x00", 3)), "//4A");
}

TEST(Base64Test, Decode) {
  EXPECT_EQ(Base64Decode("YQ=="), "a");
  EXPECT_EQ(Base64Decode("YWI="), "ab");
  EXPECT_EQ(Base64Decode("YWJj"), "abc");
  // Characters outside the alphabet are dropped.
  EXPECT_EQ(Base64Decode("Y W\nJj"), "abc");
  EXPECT_EQ(Base64Decode("!!YWJj!!"), "abc");
  // '=' counts only at the end.
  EXPECT_EQ(Base64Decode("YQ==YQ=="), "a\x06\x10");
  EXPECT_EQ(Base64Decode("Y=Q=="), "a");
  EXPECT_EQ(Base64Decode("YQ=a="), "a\x06");
  EXPECT_EQ(Base64Decode("YQ==="), "a");
  EXPECT_EQ(Base64Decode("YWJj===="), "abc");
}

TEST(Base64Test, DecodeErrors) {
  EXPECT_EQ(Base64Decode("YQ"), std::nullopt);
  EXPECT_EQ(Base64Decode("YQ==Y"), std::nullopt);
  EXPECT_EQ(Base64Decode("YWI=YQ=="), std::nullopt);
  EXPECT_EQ(Base64Decode("YQ==YWJj"), std::nullopt);
  EXPECT_EQ(Base64Decode("YWJjZA="), std::nullopt);
}

}  // namespace
}  // namespace cc::util
