#include "util/pypath.h"

#include <stdlib.h>

#include <optional>
#include <string>
#include <utility>

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

TEST(PypathTest, Normpath) {
  EXPECT_EQ(Normpath(""), ".");
  EXPECT_EQ(Normpath("a/b/"), "a/b");
  EXPECT_EQ(Normpath("//a/b"), "//a/b");
  EXPECT_EQ(Normpath("///a/../b"), "/b");
  EXPECT_EQ(Normpath("a/../../c"), "../c");
  EXPECT_EQ(Normpath("/.."), "/");
  EXPECT_EQ(Normpath("./a/./b//c/"), "a/b/c");
}

TEST(PypathTest, Basename) {
  EXPECT_EQ(Basename("/a/b.c"), "b.c");
  EXPECT_EQ(Basename("a/"), "");
  EXPECT_EQ(Basename("b"), "b");
}

TEST(PypathTest, Splitext) {
  using Parts = std::pair<std::string, std::string>;
  EXPECT_EQ(Splitext("a/.b"), Parts("a/.b", ""));
  EXPECT_EQ(Splitext("a/b.c.d"), Parts("a/b.c", ".d"));
  EXPECT_EQ(Splitext("..x"), Parts("..x", ""));
  EXPECT_EQ(Splitext("a.b/c"), Parts("a.b/c", ""));
}

TEST(PypathTest, Commonpath) {
  EXPECT_EQ(Commonpath({"/a/b/c", "/a/b/d"}), "/a/b");
  EXPECT_EQ(Commonpath({"/a/b", "/a/b/"}), "/a/b");
  EXPECT_EQ(Commonpath({"a/b", "a/./c"}), "a");
  EXPECT_EQ(Commonpath({"/a", "a"}), std::nullopt);
}

TEST(PypathTest, Expandvars) {
  setenv("CC_PYPATH_TEST", "v", 1);
  unsetenv("CC_PYPATH_UNSET");
  EXPECT_EQ(Expandvars("a/$CC_PYPATH_TEST/${CC_PYPATH_TEST}b"), "a/v/vb");
  EXPECT_EQ(Expandvars("$CC_PYPATH_UNSET ${} $ $$CC_PYPATH_TEST"),
            "$CC_PYPATH_UNSET ${} $ $v");
}

TEST(PypathTest, Realpath) {
  EXPECT_EQ(Realpath("/no/such/../file"), "/no/file");
  EXPECT_EQ(Realpath("/"), "/");
}

}  // namespace
}  // namespace cc::util::pypath
