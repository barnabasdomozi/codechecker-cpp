// Conversion to local time.

#ifndef CC_PLATFORM_LOCAL_TIME_H_
#define CC_PLATFORM_LOCAL_TIME_H_

#include <ctime>

namespace cc::platform {

// Converts `time` to the local time zone, honoring TZ like Python's
// time.localtime().
std::tm LocalTime(std::time_t time);

}  // namespace cc::platform

#endif  // CC_PLATFORM_LOCAL_TIME_H_
