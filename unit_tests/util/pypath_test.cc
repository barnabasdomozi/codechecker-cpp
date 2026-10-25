#include "util/pypath.h"

#include "gtest/gtest.h"

namespace cc::util::pypath {
namespace {

TEST(PypathTest, Dirname) {
  EXPECT_EQ(Dirname("/a/b/c"), "/a/b");
  EXPECT_EQ(Dirname("/a/b/"), "/a/b");
  EXPECT_EQ(Dirname("a"), "");
  EXPECT_EQ(Dirname("/"), "/");
  EXPECT_EQ(Dirname("//a"), "//");
  EXPECT_EQ(Dirname("a/b"), "a");
  EXPECT_EQ(Dirname("///"), "///");
  EXPECT_EQ(Dirname(""), "");
}

TEST(PypathTest, Join) {
  EXPECT_EQ(Join({"a", "b", "c"}), "a/b/c");
  EXPECT_EQ(Join({"a/", "b"}), "a/b");
  EXPECT_EQ(Join({"a", "/b", "c"}), "/b/c");
  EXPECT_EQ(Join({"", "b"}), "b");
}

}  // namespace
}  // namespace cc::util::pypath
