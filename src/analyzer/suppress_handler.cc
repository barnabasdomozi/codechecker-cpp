#include "analyzer/suppress_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/model/report.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("system");

using Entries = std::vector<SuppressEntry>;

// Returns the entries of the suppress file `text` (Python:
// get_suppress_data()). Each line is tried in the formats with a status,
// with a file name, and with neither (the old format).
Entries ParseSuppressData(std::string_view text) {
  static const util::Regex& kWithStatus = *new util::Regex(
      R"(^(?P<bug_hash>[\d\w]{32})(\#(?P<bug_hash_type>\d))?\s*\|\|\s*)"
      R"((?P<file_name>[^\\\|]+)\s*\|\|\s*(?P<comment>[^\|]*)\s*\|\|\s*)"
      // Python builds a character class of the marker names.
      R"((?P<status>[codechecker_suppress|codechecker_false_positive|)"
      R"(codechecker_intentional|codechecker_confirmed]*)$)");
  static const util::Regex& kWithFileName = *new util::Regex(
      R"(^(?P<bug_hash>[\d\w]{32})(\#(?P<bug_hash_type>\d))?\s*\|\|\s*)"
      R"((?P<file_name>[^\\\|]+)\s*\|\|\s*(?P<comment>[^\|]*)$)");
  static const util::Regex& kOld = *new util::Regex(
      R"(^(?P<bug_hash>[\d\w]{32})(\#(?P<bug_hash_type>\d))?\s*\|\|\s*)"
      R"((?P<comment>[^\|]*)$)");
  Entries entries;
  for (const std::string_view line : util::SplitLines(text)) {
    const std::string_view stripped = util::Strip(line);
    if (const std::optional<util::RegexMatch> match =
            kWithStatus.Match(stripped)) {
      entries.push_back({.report_hash = std::string(*match->Group("bug_hash")),
                         .file_name = std::string(*match->Group("file_name")),
                         .comment = std::string(*match->Group("comment")),
                         .status = std::string(*match->Group("status"))});
    } else if (const std::optional<util::RegexMatch> match =
                   kWithFileName.Match(stripped)) {
      entries.push_back({.report_hash = std::string(*match->Group("bug_hash")),
                         .file_name = std::string(*match->Group("file_name")),
                         .comment = std::string(*match->Group("comment")),
                         .status = "false_positive"});
    } else if (const std::optional<util::RegexMatch> match =
                   kOld.Match(stripped)) {
      entries.push_back({.report_hash = std::string(*match->Group("bug_hash")),
                         .file_name = "",
                         .comment = std::string(*match->Group("comment")),
                         .status = "false_positive"});
    } else if (!stripped.empty()) {
      // Python logs the line with its line end.
      kLog.Warning("Malformed suppress line: " + std::string(line));
    }
  }
  return entries;
}

// Returns the entries of the suppress file at `path`.
Result<Entries> ReadSuppressFile(const std::string& path) {
  const Result<std::string> text = util::ReadTextFile(path);
  if (!text.ok()) return Result<Entries>(text.error());
  return Result<Entries>(ParseSuppressData(text.value()));
}

}  // namespace

Result<SuppressHandler> SuppressHandler::Create(
    std::string path, bool allow_write,
    std::vector<std::string> status_filter) {
  Result<Entries> entries = ReadSuppressFile(path);
  if (!entries.ok()) return Result<SuppressHandler>(entries.error());
  return Result<SuppressHandler>(
      SuppressHandler(std::move(path), allow_write, std::move(status_filter),
                      std::move(entries).value()));
}

SuppressHandler::SuppressHandler(std::string path, bool allow_write,
                                 std::vector<std::string> status_filter,
                                 std::vector<SuppressEntry> entries)
    : path_(std::move(path)),
      allow_write_(allow_write),
      status_filter_(std::move(status_filter)),
      entries_(std::move(entries)) {}

bool SuppressHandler::IsSuppressed(const report::Report& report) const {
  if (status_filter_.empty()) return false;
  const std::string file_name =
      util::pypath::Basename(report.file.original_path);
  return std::ranges::any_of(entries_, [&](const SuppressEntry& entry) {
    return report.report_hash == entry.report_hash &&
           entry.file_name == file_name &&
           std::ranges::find(status_filter_, entry.status) ==
               status_filter_.end();
  });
}

void SuppressHandler::StoreSuppressBugId(const std::string& report_hash,
                                         const std::string& file_name,
                                         const std::string& comment,
                                         const std::string& status) {
  if (!allow_write_) return;
  kLog.Debug("Processing suppress file: " + path_);
  const Result<Entries> current = ReadSuppressFile(path_);
  Result<void> written;
  if (!current.ok()) {
    written = Result<void>(current.error());
  } else if (std::ranges::none_of(
                 current.value(), [&](const SuppressEntry& entry) {
                   return entry.report_hash == report_hash &&
                          (entry.file_name == file_name ||
                           entry.file_name.empty());
                 })) {
    written = util::AppendFile(path_, report_hash + "||" + file_name + "||" +
                                          comment + "||" + status + "\n");
  }
  if (!written.ok()) {
    kLog.Error(written.error().message);
    kLog.Error("Failed to write: " + path_);
  }
  if (Result<Entries> entries = ReadSuppressFile(path_); entries.ok()) {
    entries_ = std::move(entries).value();
  }
}

}  // namespace cc::analyzer
