#include "util/datetime.h"

#include <stdint.h>

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "util/result.h"

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

TEST(DatetimeTest, MakeTime) {
  const Result<int64_t> time = MakeTime(2026, 10, 24, 11, 40, 35);
  ASSERT_TRUE(time.ok());
  EXPECT_EQ(FormatPythonTime(time.value()), "2026-10-24 11:40:35");
  ASSERT_TRUE(MakeTime(2024, 2, 29, 0, 0, 0).ok());
  ASSERT_TRUE(MakeTime(1, 1, 1, 0, 0, 0).ok());
  ASSERT_TRUE(MakeTime(9999, 12, 31, 23, 59, 59).ok());
}

TEST(DatetimeTest, MakeTimeFailsWithPythonsMessages) {
  // The messages of datetime(), and its order of checks.
  const auto message = [](int64_t year, int64_t month, int64_t day,
                          int64_t hour, int64_t minute, int64_t second) {
    const Result<int64_t> time =
        MakeTime(year, month, day, hour, minute, second);
    return time.ok() ? std::string("ok") : time.error().message;
  };
  EXPECT_EQ(message(0, 1, 1, 0, 0, 0), "year 0 is out of range");
  EXPECT_EQ(message(10000, 13, 1, 0, 0, 0), "year 10000 is out of range");
  EXPECT_EQ(message(2020, 0, 1, 0, 0, 0), "month must be in 1..12");
  EXPECT_EQ(message(2020, 1, 0, 24, 0, 0), "day is out of range for month");
  EXPECT_EQ(message(2023, 2, 29, 0, 0, 0), "day is out of range for month");
  EXPECT_EQ(message(2020, 4, 31, 0, 0, 0), "day is out of range for month");
  EXPECT_EQ(message(2020, 1, int64_t{1} << 40, 0, 0, 0),
            "day is out of range for month");
  EXPECT_EQ(message(2020, 1, 1, 24, 60, 0), "hour must be in 0..23");
  EXPECT_EQ(message(2020, 1, 1, 0, -1, 0), "minute must be in 0..59");
  EXPECT_EQ(message(2020, 1, 1, 0, 0, 60), "second must be in 0..59");
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
