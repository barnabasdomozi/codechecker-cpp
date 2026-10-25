#include "platform/local_time.h"

#include <time.h>

#include <ctime>

namespace cc::platform {

std::tm LocalTime(std::time_t time) {
  std::tm result = {};
  localtime_r(&time, &result);
  return result;
}

}  // namespace cc::platform
