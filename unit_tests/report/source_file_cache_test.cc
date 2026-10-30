#include "report/model/source_file_cache.h"

#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "unit_tests/report/temp_folder_test.h"
#include "util/file.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::report {
namespace {

class SourceFileCacheTest : public TempFolderTest {
 protected:
  // Writes `content` into the file `name` of the test folder and returns
  // its path.
  std::string WriteSource(const std::string& name,
                          const std::string& content) {
    const std::string path = util::pypath::Join({folder(), name});
    EXPECT_TRUE(util::WriteFile(path, content).ok());
    return path;
  }
};

TEST_F(SourceFileCacheTest, LineKeepsLineEndsAndChecksBounds) {
  const std::string path = WriteSource("a.c", "first\nsecond\nlast");
  SourceFileCache sources;
  EXPECT_EQ(sources.Line(path, 1), "first\n");
  EXPECT_EQ(sources.Line(path, 2), "second\n");
  EXPECT_EQ(sources.Line(path, 3), "last");
  EXPECT_EQ(sources.Line(path, 0), "");
  EXPECT_EQ(sources.Line(path, 4), "");
}

// The text is read as Python's open(errors='ignore') reads it: universal
// newlines, and undecodable bytes dropped.
TEST_F(SourceFileCacheTest, TextIsDecodedWithUniversalNewlines) {
  const std::string path = WriteSource("b.c", "a\r\nb\xff\rc\n");
  SourceFileCache sources;
  ASSERT_NE(sources.Text(path), nullptr);
  EXPECT_EQ(*sources.Text(path), "a\nb\nc\n");
  ASSERT_NE(sources.Lines(path), nullptr);
  EXPECT_EQ(*sources.Lines(path),
            (std::vector<std::string_view>{"a\n", "b\n", "c\n"}));
}

TEST_F(SourceFileCacheTest, MissingFile) {
  SourceFileCache sources;
  const std::string path = util::pypath::Join({folder(), "missing.c"});
  EXPECT_EQ(sources.Text(path), nullptr);
  EXPECT_EQ(sources.Lines(path), nullptr);
  EXPECT_EQ(sources.Line(path, 1), "");
}

// A file is read once: later changes on disk are not seen.
TEST_F(SourceFileCacheTest, ReadsEachFileOnce) {
  const std::string path = WriteSource("c.c", "old\n");
  SourceFileCache sources;
  const std::string_view line = sources.Line(path, 1);
  WriteSource("c.c", "new\n");
  EXPECT_EQ(sources.Line(path, 1), "old\n");
  // The views stay valid while other files are added.
  for (int i = 0; i < 100; ++i) {
    sources.Line(WriteSource("f" + std::to_string(i) + ".c", "x\n"), 1);
  }
  EXPECT_EQ(line, "old\n");
}

}  // namespace
}  // namespace cc::report
