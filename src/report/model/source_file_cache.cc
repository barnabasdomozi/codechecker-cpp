#include "report/model/source_file_cache.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/file.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

}  // namespace

const std::string* SourceFileCache::Text(const std::string& path) {
  const SourceFile* file = Get(path);
  return file != nullptr ? &file->text : nullptr;
}

const std::vector<std::string_view>* SourceFileCache::Lines(
    const std::string& path) {
  const SourceFile* file = Get(path);
  return file != nullptr ? &file->lines : nullptr;
}

std::string_view SourceFileCache::Line(const std::string& path, int line) {
  const SourceFile* file = Get(path);
  // Python logs every failed read.
  if (file == nullptr) {
    kLog.Error("Failed to open file " + path);
    return "";
  }
  if (line <= 0 || static_cast<size_t>(line) > file->lines.size()) return "";
  return file->lines[line - 1];
}

const SourceFileCache::SourceFile* SourceFileCache::Get(
    const std::string& path) {
  const auto [it, inserted] = files_.try_emplace(path);
  if (inserted) {
    Result<std::string> text = util::ReadTextFile(path);
    if (text.ok()) {
      // The lines are split after the text is in its final place.
      SourceFile& file = it->second.emplace();
      file.text = std::move(text).value();
      file.lines = util::SplitLines(file.text);
    }
  }
  return it->second ? &*it->second : nullptr;
}

}  // namespace cc::report
