#include "common/skiplist_handler.h"

#include <string>
#include <string_view>
#include <utility>

#include "util/fnmatch.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/strings.h"

namespace cc::common {
namespace {

constexpr util::Logger kLog("system");

}  // namespace

SkipListHandler::SkipListHandler(std::string_view content) {
  for (const std::string_view raw_line : util::StrSplitLines(content)) {
    const std::string_view line = util::Strip(raw_line);
    if (line.empty() || line.starts_with('#')) continue;
    if (line.size() < 2 || (line[0] != '-' && line[0] != '+')) {
      kLog.Warning("Skipping malformed skipfile pattern: " + std::string(line));
      continue;
    }
    // fnmatch's "\Z" end is dropped, so a folder also matches what is in it.
    std::string pattern = util::FnmatchTranslate(
        util::pypath::Normpath(util::Strip(line.substr(1))));
    if (pattern.ends_with("\\Z") || pattern.ends_with("\\z")) {
      pattern.resize(pattern.size() - 2);
    }
    rules_.push_back({.skip = line[0] == '-',
                      .regex = util::Regex(pattern + "(?:\\/.*)?$")});
  }
}

bool SkipListHandler::ShouldSkip(std::string_view path) const {
  for (const Rule& rule : rules_) {
    if (rule.regex.Match(path)) return rule.skip;
  }
  return false;
}

bool ShouldSkip(const SkipListHandlers& handlers, std::string_view path) {
  for (const SkipListHandler& handler : handlers) {
    if (handler.ShouldSkip(path)) return true;
  }
  return false;
}

}  // namespace cc::common
