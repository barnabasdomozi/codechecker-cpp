#include "util/zip.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "platform/filesystem.h"
#include "util/file.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::util {
namespace {

TEST(ZipTest, ExtractsWithPythonsNameRules) {
  const std::string directory = *platform::MakeTempDir();
  const std::string archive_path = pypath::Join({directory, "a.zip"});
  Result<ZipArchive> archive =
      ZipArchive::Open(archive_path, ZipArchive::Mode::kWrite);
  ASSERT_TRUE(archive.ok());
  ASSERT_TRUE(archive.value().AddText("reports/x/a.plist", "plist").ok());
  ASSERT_TRUE(archive.value().AddText("/root/../home/b.c", "source").ok());
  ASSERT_TRUE(archive.value().AddText("empty/", "").ok());
  ASSERT_TRUE(archive.value().Close().ok());

  const Result<std::vector<ZipEntry>> entries = ListZipEntries(archive_path);
  ASSERT_TRUE(entries.ok());
  ASSERT_EQ(entries.value().size(), 3);
  EXPECT_EQ(entries.value()[0].name, "reports/x/a.plist");
  EXPECT_EQ(entries.value()[0].size, 5);

  const std::string output = pypath::Join({directory, "out"});
  ASSERT_TRUE(ExtractZip(archive_path, output).ok());
  EXPECT_EQ(ReadFile(pypath::Join({output, "reports/x/a.plist"})).value(),
            "plist");
  EXPECT_EQ(ReadFile(pypath::Join({output, "root/home/b.c"})).value(),
            "source");
  EXPECT_TRUE(pypath::IsDir(pypath::Join({output, "empty"})));
  EXPECT_FALSE(ExtractZip(pypath::Join({directory, "missing.zip"}), output)
                   .ok());
  (void)platform::RemoveTree(directory);
}

}  // namespace
}  // namespace cc::util
