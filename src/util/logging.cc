#include "util/logging.h"

#include <stddef.h>

#include <cctype>
#include <ctime>
#include <format>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "platform/local_time.h"
#include "platform/process.h"
#include "util/print.h"

namespace cc::util {
namespace {

// The configuration shared by all loggers. Python's logging also has a level
// per handler, but setup_logger() always sets it to the loggers' level.
struct LogConfig {
  LogLevel level;
  // The "precise" format of debug levels, instead of the "brief" one.
  bool precise;
  Stream stream;
};

// Changed only by SetupLogger(), before other threads start.
constinit LogConfig log_config = {
    .level = LogLevel::kInfo, .precise = false, .stream = Stream::kStdout};

std::string_view LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kDebugAnalyzer:
      return "DEBUG_ANALYZER";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarning:
      return "WARNING";
    case LogLevel::kError:
      return "ERROR";
    case LogLevel::kCritical:
      return "CRITICAL";
  }
  return "";
}

// Returns the level for a --verbose value; unknown values mean INFO
// (Python: validate_loglvl()).
LogLevel VerboseLevel(std::string_view verbose) {
  std::string upper(verbose);
  for (char& c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  for (const LogLevel level :
       {LogLevel::kDebug, LogLevel::kDebugAnalyzer, LogLevel::kInfo}) {
    if (upper == LevelName(level)) return level;
  }
  return LogLevel::kInfo;
}

// Returns the time as logging's asctime with the "%Y-%m-%d %H:%M:%S" date
// format.
std::string AscTime() {
  const std::tm now = platform::LocalTime(std::time(nullptr));
  char buffer[32];
  const size_t size =
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &now);
  return std::string(buffer, size);
}

// Returns the bare name of a function from its signature, as Python's
// funcName shows it: "int cc::util::Foo(int)" gives "Foo".
std::string_view FunctionName(std::string_view signature) {
  signature = signature.substr(0, signature.find('('));
  const size_t start = signature.find_last_of(": ");
  return start == std::string_view::npos ? signature
                                         : signature.substr(start + 1);
}

std::string_view FileName(std::string_view path) {
  return path.substr(path.find_last_of('/') + 1);
}

}  // namespace

void SetupLogger(std::optional<std::string_view> verbose, Stream stream) {
  const LogLevel level = verbose ? VerboseLevel(*verbose) : LogLevel::kInfo;
  log_config = {
      .level = level,
      .precise =
          level == LogLevel::kDebug || level == LogLevel::kDebugAnalyzer,
      .stream = stream};
}

LogLevel CurrentLogLevel() { return log_config.level; }

void Logger::Info(std::string_view message,
                  std::source_location location) const {
  Log(LogLevel::kInfo, message, location);
}

void Logger::Warning(std::string_view message,
                     std::source_location location) const {
  Log(LogLevel::kWarning, message, location);
}

void Logger::Log(LogLevel level, std::string_view message,
                 const std::source_location& location) const {
  if (level < log_config.level) return;
  std::string line;
  if (log_config.precise) {
    line = std::format("[{}] [{}] {{{}}} [{}] <{}> - {}:{} {}() - {}\n",
                       LevelName(level), AscTime(), name_,
                       platform::ProcessId(), platform::ThreadId(),
                       FileName(location.file_name()), location.line(),
                       FunctionName(location.function_name()), message);
  } else {
    line = std::format("[{} {}] - {}\n", LevelName(level), AscTime(), message);
  }
  static std::mutex& mutex = *new std::mutex;
  const std::lock_guard<std::mutex> lock(mutex);
  Write(log_config.stream, line);
  Flush(log_config.stream);
}

}  // namespace cc::util
