#include "util/yaml.h"

#include <yyjson.h>

#include "gtest/gtest.h"
#include "util/json.h"
#include "util/result.h"

namespace cc::util {
namespace {

TEST(YamlTest, ResolvesTypesLikePyyaml) {
  const Result<JsonDocument> document = SafeLoadYaml(
      "a: yes\nb: 0777\nc: 1:30\nd: off\ne: ~\nf: 0x1F\ng: 1.5\nh: '12'\n"
      "i: .inf\nj: 2001-12-14x\nk: [1, x]\n1: one\nl: &x {m: 1}\nn: *x\n",
      "a.yaml");
  ASSERT_TRUE(document.ok()) << document.error().message;
  const MutableJsonDocument copy = NewJsonDocument();
  EXPECT_EQ(DumpJson(yyjson_val_mut_copy(
                copy.get(), yyjson_doc_get_root(document.value().get()))),
            "{\"a\": true, \"b\": 511, \"c\": 90, \"d\": false, \"e\": null, "
            "\"f\": 31, \"g\": 1.5, \"h\": \"12\", \"i\": Infinity, "
            "\"j\": \"2001-12-14x\", \"k\": [1, \"x\"], \"1\": \"one\", "
            "\"l\": {\"m\": 1}, \"n\": {\"m\": 1}}");
}

TEST(YamlTest, EmptyStreamIsNull) {
  const Result<JsonDocument> document = SafeLoadYaml("", "a.yaml");
  ASSERT_TRUE(document.ok());
  EXPECT_TRUE(yyjson_is_null(yyjson_doc_get_root(document.value().get())));
}

TEST(YamlTest, Errors) {
  EXPECT_FALSE(SafeLoadYaml("a: [1", "a.yaml").ok());
  EXPECT_FALSE(SafeLoadYaml("a: 1\n---\nb: 2\n", "a.yaml").ok());
}

}  // namespace
}  // namespace cc::util
