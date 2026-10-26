#include "analyzer/host_check.h"

#include <errno.h>

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

// Runs `command` like Popen(...).communicate() and returns the result.
platform::RunResult Communicate(const std::vector<std::string>& command,
                                const std::string& binary) {
  return platform::RunProcess(command, {.env = EnvForBinary(binary)});
}

}  // namespace

bool CheckAnalyzer(const std::string& binary) {
  const std::vector<std::string> command = {binary, "--version"};
  const std::string joined = util::Join(command, " ");
  kLog.Debug(joined);
  const platform::RunResult result = Communicate(command, binary);
  if (result.start_error != 0) {
    if (result.start_error == ENOENT) {
      kLog.Error(util::OsError(result.start_error, result.start_error_path)
                     .message);
      kLog.Error("Failed to run: \"" + joined + "\"");
    }
    return false;
  }
  if (result.return_code == 0) return true;
  kLog.Error("Failed to run: \"" + joined + "\"");
  kLog.Error("stdout: " + util::DecodeUtf8Ignore(result.out));
  kLog.Error("stderr: " + util::DecodeUtf8Ignore(result.err));
  return false;
}

bool HasAnalyzerConfigOption(const std::string& clang,
                             std::string_view option_name) {
  const std::vector<std::string> command = {clang, "-cc1",
                                            "-analyzer-config-help"};
  kLog.DebugAnalyzer("run: \"" + util::Join(command, " ") + "\"");
  const platform::RunResult result = Communicate(command, clang);
  if (result.start_error != 0) {
    kLog.Error("Failed to run: \"" + util::Join(command, " ") + "\"");
    return false;
  }
  const std::string out = util::DecodeUtf8Ignore(result.out);
  kLog.DebugAnalyzer("stdout:\n" + out);
  kLog.DebugAnalyzer("stderr:\n" + util::DecodeUtf8Ignore(result.err));
  const Result<util::Regex> regex = util::Regex::Compile(option_name);
  const bool found = regex.ok() && regex.value().Search(out).has_value();
  if (found) {
    kLog.Debug("Config option '" + std::string(option_name) +
               "' is available.");
  }
  return found;
}

bool HasAnalyzerOption(const std::string& clang,
                       const std::vector<std::string>& feature) {
  const std::optional<std::string> source =
      platform::MakeTempFile(platform::TempDir(), "");
  if (!source || !util::WriteFile(*source, "void foo(){}").ok()) return false;
  std::vector<std::string> command = {clang, "-x", "c", "--analyze"};
  command.insert(command.end(), feature.begin(), feature.end());
  command.insert(command.end(), {*source, "-o", "-"});
  kLog.DebugAnalyzer("run: \"" + util::Join(command, " ") + "\"");
  const platform::RunResult result = Communicate(command, clang);
  std::remove(source->c_str());
  if (result.start_error != 0) {
    kLog.Error("Failed to run: \"" + util::Join(command, " ") + "\"");
    return false;
  }
  kLog.DebugAnalyzer("stdout:\n" + util::DecodeUtf8Ignore(result.out));
  kLog.DebugAnalyzer("stderr:\n" + util::DecodeUtf8Ignore(result.err));
  return result.return_code == 0;
}

}  // namespace cc::analyzer
