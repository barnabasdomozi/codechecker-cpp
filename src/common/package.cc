#include "common/package.h"

#include <string>

#include "platform/process.h"
#include "util/pypath.h"

namespace cc::common {

std::string DataFilesDir() {
  return util::pypath::Dirname(
      util::pypath::Dirname(platform::ExecutablePath()));
}

}  // namespace cc::common
