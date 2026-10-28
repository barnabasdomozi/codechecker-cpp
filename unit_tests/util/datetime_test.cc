#include "util/datetime.h"

#include <stdint.h>

#include <optional>

#include "gtest/gtest.h"

namespace cc::util {
namespace {

TEST(DatetimeTest, FormatsLikeSqlAlchemyAndPython) {
  const std::optional<int64_t> time =
      ParseDatabaseTime("2026-10-24 11:40:35.335565");
  ASSERT_TRUE(time.has_value());
  EXPECT_EQ(FormatDatabaseTime(*time), "2026-10-24 11:40:35.335565");
  EXPECT_EQ(FormatPythonTime(*time), "2026-10-24 11:40:35.335565");
  const std::optional<int64_t> whole = ParseDatabaseTime("2026-01-02 03:04:05");
  ASSERT_TRUE(whole.has_value());
  EXPECT_EQ(FormatDatabaseTime(*whole), "2026-01-02 03:04:05.000000");
  EXPECT_EQ(FormatPythonTime(*whole), "2026-01-02 03:04:05");
}

TEST(DatetimeTest, ParsesPostgresqlTimes) {
  // PostgreSQL drops the trailing zeros of the fraction.
  EXPECT_EQ(ParseDatabaseTime("2026-10-24 11:40:35.3"),
            ParseDatabaseTime("2026-10-24 11:40:35.300000"));
  EXPECT_EQ(ParseDatabaseTime("2026-10-24T11:40:35"),
            ParseDatabaseTime("2026-10-24 11:40:35"));
  EXPECT_EQ(ParseDatabaseTime("2026-10-24"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026-10-24 11:40:35.1234567"), std::nullopt);
}

TEST(DatetimeTest, RejectsInvalidDatesAndTimes) {
  EXPECT_EQ(ParseDatabaseTime("2026-13-01 00:00:00"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026-02-30 00:00:00"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026--1-01 00:00:00"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026-01-01 24:00:00"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026-01-01 00:60:00"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026-01-01 00:00:60"), std::nullopt);
  EXPECT_EQ(ParseDatabaseTime("2026-01-01 00:00:00.+5"), std::nullopt);
  EXPECT_TRUE(ParseDatabaseTime("2024-02-29 23:59:59").has_value());
}

TEST(DatetimeTest, DatabaseTimeToPython) {
  EXPECT_EQ(DatabaseTimeToPython("2026-01-02 03:04:05.000000"),
            "2026-01-02 03:04:05");
  EXPECT_EQ(DatabaseTimeToPython("2026-01-02T03:04:05.5"),
            "2026-01-02 03:04:05.500000");
  EXPECT_EQ(DatabaseTimeToPython("not a time"), "not a time");
}

TEST(DatetimeTest, Ordering) {
  EXPECT_LT(*ParseDatabaseTime("2025-12-31 23:59:59.999999"),
            *ParseDatabaseTime("2026-01-01 00:00:00"));
  EXPECT_EQ(*ParseDatabaseTime("2026-01-01 00:00:01") -
                *ParseDatabaseTime("2026-01-01 00:00:00"),
            kMicrosecondsPerSecond);
}

TEST(DatetimeTest, TimestampsRoundTrip) {
  // 2023-11-14 22:13:20 UTC, in any time zone.
  constexpr int64_t kTimestamp = 1700000000;
  const std::optional<int64_t> timestamp =
      ToTimestamp(FromTimestamp(kTimestamp));
  ASSERT_TRUE(timestamp.has_value());
  EXPECT_EQ(*timestamp, kTimestamp);
  EXPECT_EQ(FromTimestamp(kTimestamp) % kMicrosecondsPerSecond, 0);
}

TEST(DatetimeTest, FormatTimedelta) {
  EXPECT_EQ(FormatTimedelta(5 * kMicrosecondsPerSecond), "0:00:05");
  EXPECT_EQ(FormatTimedelta(2500000), "0:00:02.500000");
  EXPECT_EQ(FormatTimedelta(3600 * kMicrosecondsPerSecond), "1:00:00");
  EXPECT_EQ(FormatTimedelta(86403 * kMicrosecondsPerSecond),
            "1 day, 0:00:03");
  EXPECT_EQ(FormatTimedelta(2 * 86400 * kMicrosecondsPerSecond),
            "2 days, 0:00:00");
  EXPECT_EQ(FormatTimedelta(-kMicrosecondsPerSecond), "-1 day, 23:59:59");
  EXPECT_EQ(FormatTimedelta(1), "0:00:00.000001");
}

}  // namespace
}  // namespace cc::util
