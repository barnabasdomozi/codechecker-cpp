#include "util/plist.h"

#include <yyjson.h>

#include <string>

#include "gtest/gtest.h"
#include "util/json.h"
#include "util/result.h"

namespace cc::util {
namespace {

TEST(PlistTest, DumpsLikePlistlib) {
  const Result<JsonDocument> parsed =
      ParseJson(R"({"z": "a\r\nb", "a": [], "n": {"y": 2.5, "x": true}})");
  ASSERT_TRUE(parsed.ok());
  const MutableJsonDocument copy = NewJsonDocument();
  yyjson_mut_val* value = yyjson_val_mut_copy(
      copy.get(), yyjson_doc_get_root(parsed.value().get()));
  const Result<std::string> text = DumpPlist(value);
  ASSERT_TRUE(text.ok());
  EXPECT_EQ(text.value(),
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "\t<key>a</key>\n"
            "\t<array/>\n"
            "\t<key>n</key>\n"
            "\t<dict>\n"
            "\t\t<key>x</key>\n"
            "\t\t<true/>\n"
            "\t\t<key>y</key>\n"
            "\t\t<real>2.5</real>\n"
            "\t</dict>\n"
            "\t<key>z</key>\n"
            "\t<string>a\nb</string>\n"
            "</dict>\n"
            "</plist>\n");

  // It reads back.
  const Result<JsonDocument> read = ParsePlist(text.value(), "a.plist");
  ASSERT_TRUE(read.ok());
  yyjson_val* root = yyjson_doc_get_root(read.value().get());
  EXPECT_EQ(JsonToStr(JsonGet(root, "z")), "a\nb");
  EXPECT_TRUE(yyjson_get_bool(JsonGet(JsonGet(root, "n"), "x")));
}

TEST(PlistTest, ControlCharactersFail) {
  const MutableJsonDocument document = NewJsonDocument();
  EXPECT_FALSE(DumpPlist(yyjson_mut_str(document.get(), "a\x1b")).ok());
}

TEST(PlistTest, SyntaxErrorsHaveLxmlsMessage) {
  const Result<JsonDocument> read = ParsePlist("", "dir/a.plist");
  ASSERT_FALSE(read.ok());
  EXPECT_TRUE(read.error().message.ends_with("(a.plist, line 1)"))
      << read.error().message;
}

}  // namespace
}  // namespace cc::util
