#include "util/datetime.h"

#include <stdint.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "platform/local_time.h"

namespace cc::util {
namespace {

// Returns the time of the given wall clock date and time.
int64_t ToTime(std::chrono::year_month_day date, int64_t seconds_of_day,
               int64_t microseconds) {
  const std::chrono::sys_days days(date);
  return (int64_t{days.time_since_epoch().count()} * 86400 + seconds_of_day) *
             kMicrosecondsPerSecond +
         microseconds;
}

// Parses the `size` digits at `pos` of `text`; nullopt if they are not all
// digits.
std::optional<int> Digits(std::string_view text, size_t pos, size_t size) {
  if (pos + size > text.size()) return std::nullopt;
  const std::string_view digits = text.substr(pos, size);
  // from_chars() would also take a sign.
  const bool all_digits = std::ranges::all_of(
      digits, [](char c) { return c >= '0' && c <= '9'; });
  if (!all_digits) return std::nullopt;
  int value = 0;
  const std::from_chars_result result =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (result.ec != std::errc()) return std::nullopt;
  return value;
}

}  // namespace

int64_t LocalNow() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const int64_t microseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(
          now.time_since_epoch())
          .count() %
      kMicrosecondsPerSecond;
  const std::tm local = platform::LocalTime(seconds);
  return ToTime(std::chrono::year(local.tm_year + 1900) / (local.tm_mon + 1) /
                    local.tm_mday,
                int64_t{local.tm_hour} * 3600 + local.tm_min * 60 +
                    local.tm_sec,
                microseconds < 0 ? 0 : microseconds);
}

int64_t UtcNow() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string FormatDatabaseTime(int64_t time) {
  const int64_t seconds = time / kMicrosecondsPerSecond;
  const int64_t microseconds = time % kMicrosecondsPerSecond;
  const std::chrono::sys_seconds point{std::chrono::seconds(seconds)};
  return std::format("{:%Y-%m-%d %H:%M:%S}.{:06}", point, microseconds);
}

std::string FormatPythonTime(int64_t time) {
  std::string text = FormatDatabaseTime(time);
  if (time % kMicrosecondsPerSecond == 0) text.resize(text.size() - 7);
  return text;
}

std::optional<int64_t> ParseDatabaseTime(std::string_view text) {
  // "YYYY-MM-DD HH:MM:SS".
  if (text.size() < 19 || text[4] != '-' || text[7] != '-' ||
      (text[10] != ' ' && text[10] != 'T') || text[13] != ':' ||
      text[16] != ':') {
    return std::nullopt;
  }
  const std::optional<int> year = Digits(text, 0, 4);
  const std::optional<int> month = Digits(text, 5, 2);
  const std::optional<int> day = Digits(text, 8, 2);
  const std::optional<int> hour = Digits(text, 11, 2);
  const std::optional<int> minute = Digits(text, 14, 2);
  const std::optional<int> second = Digits(text, 17, 2);
  if (!year || !month || !day || !hour || !minute || !second) {
    return std::nullopt;
  }
  const std::chrono::year_month_day date =
      std::chrono::year(*year) / *month / *day;
  if (!date.ok() || *hour > 23 || *minute > 59 || *second > 59) {
    return std::nullopt;
  }
  int64_t microseconds = 0;
  if (text.size() > 19) {
    const size_t fraction_size = text.size() - 20;
    if (text[19] != '.' || fraction_size == 0 || fraction_size > 6) {
      return std::nullopt;
    }
    const std::optional<int> fraction = Digits(text, 20, fraction_size);
    if (!fraction) return std::nullopt;
    microseconds = *fraction;
    for (size_t i = fraction_size; i < 6; ++i) microseconds *= 10;
  }
  return ToTime(date, int64_t{*hour} * 3600 + *minute * 60 + *second,
                microseconds);
}

std::string DatabaseTimeToPython(std::string_view text) {
  const std::optional<int64_t> time = ParseDatabaseTime(text);
  return time ? FormatPythonTime(*time) : std::string(text);
}

}  // namespace cc::util
