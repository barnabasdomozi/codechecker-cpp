// Logging with the levels, logger names and line formats of the Python
// version (common-infrastructure.md §6).
//
//   constexpr util::Logger kLog("system");
//   kLog.Info(std::format("Reading config: {}", path));
//
// All loggers share one configuration, set by SetupLogger(). Lines are
// written whole, so lines from different threads never mix.

#ifndef CC_UTIL_LOGGING_H_
#define CC_UTIL_LOGGING_H_

#include <optional>
#include <source_location>
#include <string_view>

#include "util/print.h"

namespace cc::util {

enum class LogLevel {
  kDebug = 10,
  kDebugAnalyzer = 15,
  kInfo = 20,
  kWarning = 30,
  kError = 40,
  kCritical = 50,
};

// Configures every logger like Python's logger.setup_logger(). `verbose` is
// the value of --verbose ("info", "debug_analyzer" or "debug"; nullopt if
// it was not given), and `stream` is where log lines go. Without a call, the
// loggers log INFO and above to stdout in the brief format. Call it before
// other threads start.
void SetupLogger(std::optional<std::string_view> verbose, Stream stream);

// Returns the level of the loggers (Python: LOG.getEffectiveLevel()).
LogLevel CurrentLogLevel();

// A named logger, for example "system" or "analyzer".
class Logger {
 public:
  explicit constexpr Logger(std::string_view name) : name_(name) {}

  Logger(const Logger& other) = default;
  Logger& operator=(const Logger& other) = default;

  // These functions log `message` at their level. `location` is the caller,
  // shown in the debug line format; leave it to its default.
  void Info(std::string_view message,
            std::source_location location =
                std::source_location::current()) const;
  void Warning(std::string_view message,
               std::source_location location =
                   std::source_location::current()) const;

 private:
  void Log(LogLevel level, std::string_view message,
           const std::source_location& location) const;

  std::string_view name_;
};

}  // namespace cc::util

#endif  // CC_UTIL_LOGGING_H_
