#include "util/shlex.h"

#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "util/result.h"

namespace cc::util {
namespace {

TEST(ShlexTest, SplitsLikePython) {
  const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
      {"a b  c", {"a", "b", "c"}},
      {"a 'b c' d", {"a", "b c", "d"}},
      {"a \"b \\\" c\" d", {"a", "b \" c", "d"}},
      {"a\\ b", {"a b"}},
      {"''", {""}},
      {"x\"\"y", {"xy"}},
      {"\"a\\\\b\\$c\"", {"a\\b\\$c"}},
      {"a'b'c", {"abc"}},
      {"-DX=\"a b\"", {"-DX=a b"}},
      {" ", {}},
      {"-I/x\\ y z", {"-I/x y", "z"}},
  };
  for (const auto& [text, expected] : cases) {
    const Result<std::vector<std::string>> args = ShlexSplit(text);
    ASSERT_TRUE(args.ok()) << text;
    EXPECT_EQ(args.value(), expected) << text;
  }
}

TEST(ShlexTest, SplitErrors) {
  EXPECT_EQ(ShlexSplit("a\\").error().message, "No escaped character");
  EXPECT_EQ(ShlexSplit("\"abc").error().message, "No closing quotation");
}

TEST(ShlexTest, QuotesLikePython) {
  EXPECT_EQ(ShlexQuote(""), "''");
  EXPECT_EQ(ShlexQuote("abc"), "abc");
  EXPECT_EQ(ShlexQuote("a b"), "'a b'");
  EXPECT_EQ(ShlexQuote("it's"), "'it'\"'\"'s'");
  EXPECT_EQ(ShlexQuote("a@%+=:,./-_"), "a@%+=:,./-_");
  EXPECT_EQ(ShlexQuote("\xc3\xbc"), "'\xc3\xbc'");
  EXPECT_EQ(ShlexQuote("$x"), "'$x'");
  EXPECT_EQ(ShlexJoin({"a b", "c"}), "'a b' c");
}

}  // namespace
}  // namespace cc::util
