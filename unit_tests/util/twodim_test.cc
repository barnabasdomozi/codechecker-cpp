#include "util/twodim.h"

#include <stdlib.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace cc::util::twodim {
namespace {

using Rows = std::vector<std::vector<std::string>>;

const Rows kVersionRows = {
    {"Base package version", "6.30.0"},
    {"Package build date", "2026-10-23T22:59"},
    {"Git commit ID (hash)", "451b0c5c1786ed21b0685c298f759164a8c5d379"},
    {"Git tag information", "6.30"},
    {"Server supported Thrift API version", "6.74"},
    {"Client Thrift API version", "6.74"}};

// Sets COLUMNS for one test.
class TwodimTest : public testing::Test {
 protected:
  void SetColumns(const char* columns) { setenv("COLUMNS", columns, 1); }
  void TearDown() override { unsetenv("COLUMNS"); }
};

TEST_F(TwodimTest, TableThatFits) {
  SetColumns("120");
  EXPECT_EQ(
      ToString("table", {"Kind", "Version"}, kVersionRows),
      "┌─────────────────────────────────────┬──────────────────────────────────────────┐\n"
      "│ Kind                                │ Version                                  │\n"
      "├─────────────────────────────────────┼──────────────────────────────────────────┤\n"
      "│ Base package version                │ 6.30.0                                   │\n"
      "│ Package build date                  │ 2026-10-23T22:59                         │\n"
      "│ Git commit ID (hash)                │ 451b0c5c1786ed21b0685c298f759164a8c5d379 │\n"
      "│ Git tag information                 │ 6.30                                     │\n"
      "│ Server supported Thrift API version │ 6.74                                     │\n"
      "│ Client Thrift API version           │ 6.74                                     │\n"
      "└─────────────────────────────────────┴──────────────────────────────────────────┘");
}

TEST_F(TwodimTest, TableShortensTheWideColumn) {
  SetColumns("80");
  EXPECT_EQ(
      ToString("table", {"Kind", "Version"}, kVersionRows),
      "┌─────────────────────────────────────┬────────────────────────────────────────┐\n"
      "│ Kind                                │ Version                                │\n"
      "├─────────────────────────────────────┼────────────────────────────────────────┤\n"
      "│ Base package version                │ 6.30.0                                 │\n"
      "│ Package build date                  │ 2026-10-23T22:59                       │\n"
      "│ Git commit ID (hash)                │ 451b0c5c1786ed21b0685c298f759164a8c5d3 │\n"
      "│                                     │ 79                                     │\n"
      "│ Git tag information                 │ 6.30                                   │\n"
      "│ Server supported Thrift API version │ 6.74                                   │\n"
      "│ Client Thrift API version           │ 6.74                                   │\n"
      "└─────────────────────────────────────┴────────────────────────────────────────┘");
}

TEST_F(TwodimTest, TableSharesTheWidthInProportion) {
  SetColumns("40");
  EXPECT_EQ(ToString("table", {"Kind", "Version"}, kVersionRows),
            "┌─────────────────┬───────────────────┐\n"
            "│ Kind            │ Version           │\n"
            "├─────────────────┼───────────────────┤\n"
            "│ Base package    │ 6.30.0            │\n"
            "│ version         │                   │\n"
            "│ Package build   │ 2026-10-23T22:59  │\n"
            "│ date            │                   │\n"
            "│ Git commit ID   │ 451b0c5c1786ed21b │\n"
            "│ (hash)          │ 0685c298f759164a8 │\n"
            "│                 │ c5d379            │\n"
            "│ Git tag         │ 6.30              │\n"
            "│ information     │                   │\n"
            "│ Server          │ 6.74              │\n"
            "│ supported       │                   │\n"
            "│ Thrift API      │                   │\n"
            "│ version         │                   │\n"
            "│ Client Thrift   │ 6.74              │\n"
            "│ API version     │                   │\n"
            "└─────────────────┴───────────────────┘");
}

TEST_F(TwodimTest, TableNarrowerThanItsHeader) {
  // The header widths are not limited, so the whole table is scaled down
  // and the header is cut.
  SetColumns("10");
  EXPECT_EQ(ToString("table", {"Kind", "Version"}, {{"Ab c", "6.30.0"}}),
            "┌───┬────┐\n"
            "│ K │ Ve │\n"
            "├───┼────┤\n"
            "│ A │ 6. │\n"
            "│ b │ 30 │\n"
            "│ c │ .0 │\n"
            "└───┴────┘");
}

TEST_F(TwodimTest, EmptyTable) {
  EXPECT_EQ(ToString("table", {"Kind", "Version"}, {}),
            "┌──────┬─────────┐\n"
            "│ Kind │ Version │\n"
            "├──────┼─────────┤\n"
            "└──────┴─────────┘");
}

TEST_F(TwodimTest, TableWithLinesAndTabs) {
  EXPECT_EQ(ToString("table", {"A", "B"}, {{"x\ty", "line1\nline2"}}),
            "┌───────────┬───────┐\n"
            "│ A         │ B     │\n"
            "├───────────┼───────┤\n"
            "│ x       y │ line1 │\n"
            "│           │ line2 │\n"
            "└───────────┴───────┘");
}

TEST_F(TwodimTest, TableWithoutHeaderWithSeparatedRows) {
  SetColumns("120");
  EXPECT_EQ(ToTable({{"Number of processed analyzer result files", "9"},
                     {"Number of analyzer reports", "6"}},
                    {.show_header = false, .rule_between_rows = true}),
            "┌───────────────────────────────────────────┬───┐\n"
            "│ Number of processed analyzer result files │ 9 │\n"
            "├───────────────────────────────────────────┼───┤\n"
            "│ Number of analyzer reports                │ 6 │\n"
            "└───────────────────────────────────────────┴───┘");
  // One row has nothing to separate.
  EXPECT_EQ(ToTable({{"only row", "1"}},
                    {.show_header = false, .rule_between_rows = true}),
            "┌──────────┬───┐\n"
            "│ only row │ 1 │\n"
            "└──────────┴───┘");
}

TEST_F(TwodimTest, TableWithHeaderAndSeparatedRows) {
  SetColumns("120");
  EXPECT_EQ(ToTable({{"A", "B"}, {"x", "1"}, {"y", "2"}},
                    {.show_header = true, .rule_between_rows = true}),
            "┌───┬───┐\n"
            "│ A │ B │\n"
            "├───┼───┤\n"
            "│ x │ 1 │\n"
            "├───┼───┤\n"
            "│ y │ 2 │\n"
            "└───┴───┘");
}

TEST_F(TwodimTest, Rows) {
  const Rows rows(kVersionRows.begin(), kVersionRows.begin() + 2);
  EXPECT_EQ(ToString("rows", {"Kind", "Version"}, rows),
            " Base package version 6.30.0\n"
            " Package build date 2026-10-23T22:59");
  // Only the middle columns are padded.
  EXPECT_EQ(ToString("rows", {"A", "B", "C"},
                     {{"a", "bb", "c"}, {"aaa", "b", ""}}),
            " a bb c\n aaa b  ");
}

TEST_F(TwodimTest, Csv) {
  const Rows rows(kVersionRows.begin(), kVersionRows.begin() + 2);
  EXPECT_EQ(ToString("csv", {"Kind", "Version"}, rows),
            "Kind,Version\n"
            "Base package version,6.30.0\n"
            "Package build date,2026-10-23T22:59");
}

TEST_F(TwodimTest, Json) {
  const Rows rows(kVersionRows.begin(), kVersionRows.begin() + 2);
  EXPECT_EQ(ToString("json", {"Kind", "Version"}, rows),
            R"([{"Kind": "Base package version", "Version": "6.30.0"}, )"
            R"({"Kind": "Package build date", )"
            R"("Version": "2026-10-23T22:59"}])");
}

TEST_F(TwodimTest, KeysForFormat) {
  const std::vector<std::string> keys = {"Profile name", "Description"};
  EXPECT_EQ(KeysForFormat("csv", keys),
            (std::vector<std::string>{"profile_name", "description"}));
  EXPECT_EQ(KeysForFormat("json", keys),
            (std::vector<std::string>{"profile_name", "description"}));
  EXPECT_EQ(KeysForFormat("rows", keys), keys);
  EXPECT_EQ(KeysForFormat("table", keys), keys);
}

}  // namespace
}  // namespace cc::util::twodim
