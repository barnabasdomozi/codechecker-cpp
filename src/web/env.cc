#include "web/env.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "platform/filesystem.h"
#include "platform/terminal.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/strings.h"

namespace cc::web {
namespace {

constexpr util::Logger kLog("system");

}  // namespace

std::string DefaultWorkspace() {
  return util::pypath::Join({platform::HomeDirectory(), ".codechecker"});
}

bool CheckFileOwnerReadWrite(const std::string& path) {
  // The read and write bits of the group and of the others.
  constexpr int kGroupOrOtherReadWrite = 0066;
  const std::optional<int> mode = platform::FileMode(path);
  if (!mode || (*mode & kGroupOrOtherReadWrite) == 0) return true;
  kLog.Warning(std::format(
      "'{}' is readable by users other than you! This poses a risk of "
      "leaking sensitive information, such as passwords, session tokens, "
      "etc.!\nPlease 'chmod 0600 {}' so only you can access the file.",
      path, path));
  return false;
}

bool GetUserInput(std::string_view message) {
  util::Write(util::Stream::kStdout, message);
  util::Flush(util::Stream::kStdout);
  const std::optional<std::string> answer = platform::ReadStdinLine();
  if (!answer) return false;
  const std::string lower = util::AsciiLower(*answer);
  return lower == "y" || lower == "yes";
}

}  // namespace cc::web
