#include "analyzer/context/run_command.h"

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "platform/subprocess.h"
#include "util/file.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer {
namespace {

std::string Decode(const std::string& bytes) {
  return util::TranslateNewlines(util::DecodeUtf8Ignore(bytes));
}

}  // namespace

Result<CommandOutput> RunProc(const std::vector<std::string>& command,
                              const RunProcOptions& options) {
  const platform::RunResult result = platform::RunProcess(
      command,
      {.cwd = options.cwd,
       .env = options.env ? *options.env : EnvForBinary(command[0]),
       .new_session = true,
       .timeout = options.timeout,
       .on_timeout = options.on_timeout});
  if (result.start_error != 0) {
    return Result<CommandOutput>(
        util::OsError(result.start_error, result.start_error_path));
  }
  return Result<CommandOutput>(CommandOutput{.return_code = result.return_code,
                                             .out = Decode(result.out),
                                             .err = Decode(result.err)});
}

Result<std::string> CheckOutput(const std::vector<std::string>& command,
                                platform::Output stderr_mode) {
  const platform::RunResult result = platform::RunProcess(
      command,
      {.env = EnvForBinary(command[0]), .stderr_mode = stderr_mode});
  if (result.start_error != 0) {
    return Result<std::string>(
        util::OsError(result.start_error, result.start_error_path));
  }
  if (result.return_code != 0) {
    std::vector<std::string> items;
    for (const std::string& arg : command) items.push_back(util::Repr(arg));
    return Result<std::string>(Error{
        .message = std::format("Command '[{}]' returned non-zero exit status "
                               "{}.",
                               util::Join(items, ", "), result.return_code)});
  }
  return Result<std::string>(Decode(result.out));
}

}  // namespace cc::analyzer
