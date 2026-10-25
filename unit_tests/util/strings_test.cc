#include "util/strings.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

TEST(StringsTest, PythonWhitespace) {
  EXPECT_EQ(Strip("  a\u3000b \x1c\n"), "a\u3000b");
  EXPECT_EQ(RightStrip(" a \t"), " a");
  EXPECT_EQ(RemoveWhitespace("a\x1c" "b\u00a0c\u2003d \t"), "abcd");
}

TEST(StringsTest, Lines) {
  EXPECT_EQ(TranslateNewlines("a\r\nb\rc\n"), "a\nb\nc\n");
  EXPECT_EQ(SplitLines("a\r\nb\rc\nd"),
            (std::vector<std::string_view>{"a\r\n", "b\r", "c\n", "d"}));
}

TEST(StringsTest, FloatRepr) {
  EXPECT_EQ(FloatRepr(1.0), "1.0");
  EXPECT_EQ(FloatRepr(1e-05), "1e-05");
  EXPECT_EQ(FloatRepr(123456789012345680.0), "1.2345678901234568e+17");
  EXPECT_EQ(FloatRepr(0.1), "0.1");
  EXPECT_EQ(FloatRepr(1e16), "1e+16");
}

}  // namespace
}  // namespace cc::util
