#include "util/fnmatch.h"

#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace cc::util {
namespace {

TEST(FnmatchTest, TranslatesLikePython) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"*.c", R"((?s:.*\.c)\Z)"},
      {"*/lib/*", R"((?s:(?>.*?/lib/).*)\Z)"},
      {"a?c", R"((?s:a.c)\Z)"},
      {"[abc]", R"((?s:[abc])\Z)"},
      {"[!a-c]x", R"((?s:[^a-c]x)\Z)"},
      {"[z-a]", R"((?s:(?!))\Z)"},
      {"[]]", R"((?s:[]])\Z)"},
      {"[!]", R"((?s:\[!\])\Z)"},
      {"a[", R"((?s:a\[)\Z)"},
      {"*a*b", R"((?s:(?>.*?a).*b)\Z)"},
      {"**", R"((?s:.*)\Z)"},
      {"\xc3\xa9*", "(?s:\xc3\xa9.*)\\Z"},
      {"[a-c-e]", R"((?s:[a-c\-e])\Z)"},
      {"x.y+z", R"((?s:x\.y\+z)\Z)"},
      {"[\\]", R"((?s:[\\])\Z)"},
  };
  for (const auto& [pattern, expected] : cases) {
    EXPECT_EQ(FnmatchTranslate(pattern), expected) << pattern;
  }
}

TEST(FnmatchTest, Matches) {
  EXPECT_TRUE(FnMatch("/a/b/c.c", "*.c"));
  EXPECT_TRUE(FnMatch("/a/lib/x/y.c", "*/lib/*"));
  EXPECT_FALSE(FnMatch("/a/lib", "*/lib/*"));
  EXPECT_FALSE(FnMatch("a.C", "*.c"));
  EXPECT_TRUE(FnMatch("abc", "[a-c]b?"));
}

}  // namespace
}  // namespace cc::util
