#include "util/template.h"

#include <functional>
#include <map>
#include <string>

#include "gtest/gtest.h"
#include "util/result.h"

namespace cc::util {
namespace {

// Returns the values that the tests substitute.
std::map<std::string, std::string, std::less<>> Values() {
  return {{"a", "1"}, {"b", "2"}, {"_x1y", "3"}};
}

// Returns the error message of substituting `text`, or "" on success.
std::string ErrorOf(const std::string& text) {
  const Result<std::string> result = SubstituteTemplate(text, Values());
  return result.ok() ? "" : result.error().message;
}

TEST(TemplateTest, Substitutes) {
  const Result<std::string> result =
      SubstituteTemplate("$a ${b}c $$ $_x1y", Values());
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "1 2c $ 3");
}

TEST(TemplateTest, Errors) {
  EXPECT_EQ(ErrorOf("a $"),
            "ValueError: Invalid placeholder in string: line 1, col 3");
  EXPECT_EQ(ErrorOf("x\n$1"),
            "ValueError: Invalid placeholder in string: line 2, col 1");
  EXPECT_EQ(ErrorOf("ab\n${a"),
            "ValueError: Invalid placeholder in string: line 2, col 1");
  EXPECT_EQ(ErrorOf("$"),
            "ValueError: Invalid placeholder in string: line 1, col 1");
  EXPECT_EQ(ErrorOf("ab\n\ncd $ e"),
            "ValueError: Invalid placeholder in string: line 3, col 4");
  EXPECT_EQ(ErrorOf("$zz"), "KeyError: 'zz'");
}

}  // namespace
}  // namespace cc::util
