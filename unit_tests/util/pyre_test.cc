#include "util/pyre.h"

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "util/result.h"

namespace cc::util {
namespace {

TEST(PyreTest, MatchIsAnchoredAtTheStart) {
  const Regex regex(R"((?P<path>[\S]+?):(?P<line>\d+)(:(?P<column>\d+))?)");
  EXPECT_FALSE(regex.Match("  at a.c:3").has_value());
  const std::optional<RegexMatch> match = regex.Search("  at a.c:3");
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ(match->Group("path"), "a.c");
  EXPECT_EQ(match->Group("line"), "3");
  EXPECT_EQ(match->Group("column"), std::nullopt);
  EXPECT_EQ(match->GroupCount(), 4);
}

TEST(PyreTest, PythonEscapes) {
  // \Z is the end of the text, \v the vertical tab, \u a code point.
  EXPECT_TRUE(Regex(R"(a\Z)").Match("a").has_value());
  EXPECT_FALSE(Regex(R"(a\Z)").Match("a\n").has_value());
  EXPECT_TRUE(Regex(R"(a\v)").FullMatch("a\v").has_value());
  EXPECT_TRUE(Regex(R"(é)").FullMatch("\xc3\xa9").has_value());
  // $ also matches before a final newline, as in Python.
  EXPECT_TRUE(Regex("a$").Match("a\n").has_value());
}

TEST(PyreTest, UnicodeClasses) {
  EXPECT_TRUE(Regex(R"(\s)").FullMatch("\xe2\x80\x83").has_value());
  EXPECT_TRUE(Regex(R"(\w+)").FullMatch("\xc3\xa9t\xc3\xa9").has_value());
}

TEST(PyreTest, ReplaceAll) {
  const Regex regex("0x[0-9a-fA-F]+");
  EXPECT_EQ(regex.ReplaceAll("at 0x1F and 0xab!", ""), "at  and !");
  EXPECT_EQ(regex.ReplaceAll("none", "x"), "none");
}

TEST(PyreTest, CompileError) {
  EXPECT_FALSE(Regex::Compile("(a").ok());
}

}  // namespace
}  // namespace cc::util
