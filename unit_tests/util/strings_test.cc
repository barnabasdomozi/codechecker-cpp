#include "util/strings.h"

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "util/unicode.h"

namespace cc::util {
namespace {

TEST(StringsTest, Repr) {
  EXPECT_EQ(Repr("foo"), "'foo'");
  EXPECT_EQ(Repr("it's"), "\"it's\"");
  EXPECT_EQ(Repr("say \"hi\""), "'say \"hi\"'");
  EXPECT_EQ(Repr("both ' \""), "'both \\' \"'");
  EXPECT_EQ(Repr("tab\t\\"), "'tab\\t\\\\'");
  EXPECT_EQ(Repr("\x01é"), "'\\x01é'");
}

TEST(StringsTest, ParseInt) {
  EXPECT_EQ(ParseInt("80"), 80);
  EXPECT_EQ(ParseInt(" -3\n"), -3);
  EXPECT_EQ(ParseInt("+7"), 7);
  EXPECT_EQ(ParseInt(""), std::nullopt);
  EXPECT_EQ(ParseInt("8x"), std::nullopt);
  EXPECT_EQ(ParseInt("+-1"), std::nullopt);
}

TEST(StringsTest, ExpandTabs) {
  EXPECT_EQ(ExpandTabs("a\tb\n12345678\tc"), "a       b\n12345678        c");
}

TEST(UnicodeTest, DecodeIgnoreDropsInvalidBytes) {
  EXPECT_EQ(DecodeUtf8Ignore("a\xff" "b\xe2\x82" "c\xed\xa0\x80" "d€"),
            "abcd€");
}

TEST(UnicodeTest, CodePoints) {
  EXPECT_EQ(CodePointCount("árvíz"), 5u);
  EXPECT_EQ(CodePointPrefixSize("árvíz", 2), 3u);
}

}  // namespace
}  // namespace cc::util
