#ifndef CC_UNIT_TESTS_REPORT_TEMP_FOLDER_TEST_H_
#define CC_UNIT_TESTS_REPORT_TEMP_FOLDER_TEST_H_

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "platform/filesystem.h"

namespace cc::report {

// A test fixture that gives each test a new, empty temporary folder and
// deletes it after the test. A fixture that overrides SetUp() calls
// ASSERT_NO_FATAL_FAILURE(TempFolderTest::SetUp()) first.
class TempFolderTest : public testing::Test {
 protected:
  void SetUp() override {
    const std::optional<std::string> folder = platform::MakeTempDir();
    ASSERT_TRUE(folder.has_value());
    folder_ = *folder;
  }

  void TearDown() override {
    if (!folder_.empty()) {
      (void)platform::RemoveTree(folder_);
    }
  }

  const std::string& folder() const { return folder_; }

 private:
  std::string folder_;
};

}  // namespace cc::report

#endif  // CC_UNIT_TESTS_REPORT_TEMP_FOLDER_TEST_H_
