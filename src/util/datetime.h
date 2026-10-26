// Naive local date and times, as Python's datetime.now() gives them and
// SQLAlchemy stores them in the server's databases (server-database.md §4).
//
// A time is the number of microseconds since 1970-01-01 00:00:00 of the
// local wall clock: two times can be compared and subtracted, but they do
// not name an instant of UTC.

#ifndef CC_UTIL_DATETIME_H_
#define CC_UTIL_DATETIME_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

namespace cc::util {

// One second in the unit of the times.
inline constexpr int64_t kMicrosecondsPerSecond = 1000000;

// Returns the current local time (Python: datetime.now()).
int64_t LocalNow();

// Returns the current time in UTC, in the same form (Python:
// datetime.now(timezone.utc) without its time zone).
int64_t UtcNow();

// Returns `time` as "YYYY-MM-DD HH:MM:SS.ffffff", the form SQLAlchemy stores
// datetimes in on SQLite, which PostgreSQL also accepts.
std::string FormatDatabaseTime(int64_t time);

// Returns `time` as Python's str(datetime) writes it: like
// FormatDatabaseTime(), but without ".ffffff" when the microseconds are 0.
std::string FormatPythonTime(int64_t time);

// Parses "YYYY-MM-DD HH:MM:SS" with an optional fraction of up to 6 digits,
// and a "T" instead of the space; nullopt for other texts and for dates or
// times that do not exist.
std::optional<int64_t> ParseDatabaseTime(std::string_view text);

// Returns the database time `text` as FormatPythonTime() writes it; `text`
// itself if it cannot be parsed.
std::string DatabaseTimeToPython(std::string_view text);

}  // namespace cc::util

#endif  // CC_UTIL_DATETIME_H_
