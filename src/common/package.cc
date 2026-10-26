#include "common/package.h"

#include <string>
#include <utility>
#include <vector>

#include "platform/process.h"
#include "util/pypath.h"

namespace cc::common {

std::string DataFilesDir() {
  return util::pypath::Dirname(
      util::pypath::Dirname(platform::ExecutablePath()));
}

namespace {

std::vector<std::string>& CommandLineStorage() {
  static std::vector<std::string>& args = *new std::vector<std::string>;
  return args;
}

}  // namespace

void SetCommandLine(std::vector<std::string> args) {
  CommandLineStorage() = std::move(args);
}

const std::vector<std::string>& CommandLine() { return CommandLineStorage(); }

}  // namespace cc::common
