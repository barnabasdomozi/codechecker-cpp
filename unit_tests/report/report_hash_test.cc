#include "report/hash/report_hash.h"

#include <stdlib.h>

#include <string>

#include "gtest/gtest.h"
#include "report/model/report.h"
#include "report/model/source_file_cache.h"
#include "util/file.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::report {
namespace {

// The verified examples of report-hash.md.
class ReportHashTest : public testing::Test {
 protected:
  void SetUp() override {
    char folder[] = "/tmp/cc-report-hash-XXXXXX";
    ASSERT_NE(mkdtemp(folder), nullptr);
    path_ = util::pypath::Join({folder, "main.c"});
    std::string content;
    for (int line = 1; line <= 30; ++line) {
      if (line == 11) {
        content += "  return a / z;\n";
      } else if (line == 21) {
        content += "  int unused = 5;\n";
      } else {
        content += "\n";
      }
    }
    ASSERT_TRUE(util::WriteFile(path_, content).ok());
  }

  std::string path_;
};

TEST_F(ReportHashTest, ContextFree) {
  const Report report = MakeReport(MakeFile(path_), 11, 12,
                                   "Division by zero", "core.DivideZero");
  SourceFileCache sources;
  EXPECT_EQ(GetReportHash(report, HashType::kContextFree, sources),
            "d11a6fc279b8946a4bdc0365ff032ae0");
}

TEST_F(ReportHashTest, PathSensitive) {
  const Report report =
      MakeReport(MakeFile(path_), 21, 7, "unused variable 'unused'",
                 "clang-diagnostic-unused-variable");
  SourceFileCache sources;
  EXPECT_EQ(GetReportHash(report, HashType::kPathSensitive, sources),
            "249a121710b6a5cb467697f323de0d5f");
}

}  // namespace
}  // namespace cc::report
