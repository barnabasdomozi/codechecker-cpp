// Source files read once for the many reports that refer to them.

#ifndef CC_REPORT_MODEL_SOURCE_FILE_CACHE_H_
#define CC_REPORT_MODEL_SOURCE_FILE_CACHE_H_

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cc::report {

// The source files of reports, each read from disk on first use and kept
// until the cache is destroyed. The reports of one result file refer to a
// few files many times, so a cache usually lives while the reports of one
// result file are handled. Not thread-safe.
//
// Example:
//   SourceFileCache sources;
//   for (const Report& report : reports) {
//     Print(sources.Line(report.file.original_path, report.line));
//   }
class SourceFileCache {
 public:
  SourceFileCache() = default;
  SourceFileCache(const SourceFileCache&) = delete;
  SourceFileCache& operator=(const SourceFileCache&) = delete;

  // Returns the text of the file at `path` as util::ReadTextFile() reads it;
  // nullptr if the file cannot be read. It stays valid while the cache
  // lives.
  const std::string* Text(const std::string& path);

  // Returns the lines of Text(path), each with its line end (see
  // util::SplitLines()); nullptr if the file cannot be read. They stay valid
  // while the cache lives.
  const std::vector<std::string_view>* Lines(const std::string& path);

  // Returns line `line` (from 1) of the file at `path`, with its line end;
  // "" if the file has fewer lines or cannot be read, which is logged
  // (Python: util.get_line()). It stays valid while the cache lives.
  std::string_view Line(const std::string& path, int line);

 private:
  struct SourceFile {
    std::string text;
    std::vector<std::string_view> lines;  // Views into `text`.
  };

  // Returns the file at `path`, read on first use; nullptr if it cannot be
  // read.
  const SourceFile* Get(const std::string& path);

  // The files by path; nullopt for those that cannot be read. The map never
  // moves its elements, so the views into them stay valid.
  std::unordered_map<std::string, std::optional<SourceFile>> files_;
};

}  // namespace cc::report

#endif  // CC_REPORT_MODEL_SOURCE_FILE_CACHE_H_
