#include "util/textwrap.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace cc::util {
namespace {

using Lines = std::vector<std::string>;

TEST(TextwrapTest, WrapsAtSpaces) {
  EXPECT_EQ(
      Wrap("Print the version of CodeChecker package that is being used.", 54),
      (Lines{"Print the version of CodeChecker package that is being",
             "used."}));
}

TEST(TextwrapTest, BreaksLongWords) {
  EXPECT_EQ(Wrap("451b0c5c1786ed21b0685c298f759164a8c5d379", 38),
            (Lines{"451b0c5c1786ed21b0685c298f759164a8c5d3", "79"}));
  EXPECT_EQ(Wrap("--------x", 4), (Lines{"----", "----", "x"}));
}

TEST(TextwrapTest, BreaksLongWordsAfterHyphen) {
  EXPECT_EQ(Wrap("2026-10-23T22:59", 10), (Lines{"2026-10-", "23T22:59"}));
  EXPECT_EQ(Wrap("--analyzers", 4), (Lines{"--an", "alyz", "ers"}));
}

TEST(TextwrapTest, SplitsHyphenatedWords) {
  EXPECT_EQ(Wrap("aaaa-bbbbbbbbbbbb", 10), (Lines{"aaaa-bbbbb", "bbbbbbb"}));
  EXPECT_EQ(Wrap("clang-tidy and clang-analyzer-core", 12),
            (Lines{"clang-tidy", "and clang-", "analyzer-", "core"}));
  EXPECT_EQ(Wrap("ab-c-d-efg x", 4), (Lines{"ab-", "c-d-", "efg", "x"}));
}

TEST(TextwrapTest, SplitsAtEmDashes) {
  EXPECT_EQ(Wrap("Look, goof-ball -- use the -b option!", 10),
            (Lines{"Look,", "goof-ball", "-- use the", "-b option!"}));
  EXPECT_EQ(Wrap("a--b c", 3), (Lines{"a--", "b c"}));
}

TEST(TextwrapTest, TurnsWhitespaceIntoSpaces) {
  EXPECT_EQ(Wrap("  leading   spaces\tand\ttabs\nnewline  ", 12),
            (Lines{"  leading", "spaces", "and     tabs", "newline"}));
}

TEST(TextwrapTest, CountsCodePoints) {
  // The first line keeps a trailing space, as in Python.
  EXPECT_EQ(Wrap("árvíztűrő tükörfúrógép xyz", 10),
            (Lines{"árvíztűrő ", "tükörfúróg", "ép xyz"}));
}

TEST(TextwrapTest, EmptyText) {
  EXPECT_EQ(Wrap("", 10), Lines{});
  EXPECT_EQ(Wrap("   ", 10), Lines{});
}

TEST(TextwrapTest, Indent) {
  EXPECT_EQ(Wrap("one two three four", 9, "  "),
            (Lines{"  one two", "  three", "  four"}));
}

}  // namespace
}  // namespace cc::util
