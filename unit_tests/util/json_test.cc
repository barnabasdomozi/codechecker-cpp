#include "util/json.h"

#include <yyjson.h>

#include <string>

#include "gtest/gtest.h"
#include "util/result.h"

namespace cc::util {
namespace {

// Parses `text` into a mutable document and writes it back.
std::string RoundTrip(const std::string& text) {
  const Result<JsonDocument> document = ParseJson(text);
  EXPECT_TRUE(document.ok()) << document.error().message;
  const MutableJsonDocument copy(
      yyjson_doc_mut_copy(document.value().get(), nullptr));
  return DumpJson(yyjson_mut_doc_get_root(copy.get()));
}

TEST(JsonTest, DumpsLikePython) {
  const std::string json =
      R"({"n": null, "t": true, "f": false, "e": [], "o": {}})";
  EXPECT_EQ(RoundTrip(json), json);
}

TEST(JsonTest, DumpsNumbersLikePython) {
  EXPECT_EQ(RoundTrip("[1, -2, 1.5, 1e16, 1e-05, 0.0001, 123456789012345.6, "
                      "-0.0, NaN, Infinity, -Infinity, 100.0, "
                      "123456789012345678901234567890]"),
            "[1, -2, 1.5, 1e+16, 1e-05, 0.0001, 123456789012345.6, -0.0, NaN, "
            "Infinity, -Infinity, 100.0, 123456789012345678901234567890]");
}

TEST(JsonTest, EscapesStringsLikePython) {
  EXPECT_EQ(
      RoundTrip(R"(["q\"b\\n\n\t\u0001\u007f é 😀"])"),
      R"(["q\"b\\n\n\t\u0001\u007f \u00e9 \ud83d\ude00"])");
}

TEST(JsonTest, ReportsErrorPosition) {
  const Result<JsonDocument> document = ParseJson("{\n  \"a\": x\n}");
  ASSERT_FALSE(document.ok());
  EXPECT_TRUE(document.error().message.ends_with(": line 2 column 8 (char 9)"))
      << document.error().message;
}

}  // namespace
}  // namespace cc::util
