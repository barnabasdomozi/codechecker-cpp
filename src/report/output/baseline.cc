#include "report/output/baseline.h"

#include <format>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

// Returns the stripped lines of `text`, as Python reads a baseline file.
std::vector<std::string> ReportHashes(std::string_view text) {
  std::vector<std::string> hashes;
  // Every line of readlines() counts, also the empty last one of "a\n\n".
  for (const std::string_view line : util::SplitLines(text)) {
    hashes.emplace_back(util::Strip(line));
  }
  return hashes;
}

}  // namespace

bool IsBaselineFile(std::string_view path) {
  return path.ends_with(".baseline");
}

std::vector<std::string> BaselineHashes(const std::vector<Report>& reports) {
  std::set<std::string> hashes;
  for (const Report& report : reports) {
    if (report.report_hash) hashes.insert(*report.report_hash);
  }
  return std::vector<std::string>(hashes.begin(), hashes.end());
}

Result<void> WriteBaseline(const std::string& path,
                           const std::vector<std::string>& report_hashes) {
  // Python opens the file in "a+" mode, which creates it.
  if (Result<void> created = util::AppendFile(path, ""); !created.ok()) {
    return created;
  }
  const Result<std::string> content = util::ReadTextFile(path);
  if (!content.ok()) return Result<void>(content.error());
  const std::vector<std::string> old_hashes = ReportHashes(content.value());
  std::set<std::string> all_hashes(old_hashes.begin(), old_hashes.end());
  std::vector<std::string> new_hashes;
  for (const std::string& hash : std::set<std::string>(report_hashes.begin(),
                                                       report_hashes.end())) {
    if (all_hashes.insert(hash).second) new_hashes.push_back(hash);
  }
  if (new_hashes.empty()) {
    kLog.Info(std::format("Baseline file ({}) is up-to-date.", path));
    return Result<void>();
  }
  kLog.Info((old_hashes.empty() ? "Creating new baseline file: "
                                : "Merging existing baseline file: ") +
            path);
  kLog.Info(std::format("Total number of old report hashes: {}",
                        old_hashes.size()));
  kLog.Info(std::format("Total number of new report hashes: {}",
                        new_hashes.size()));
  std::vector<std::string> reprs;
  for (const std::string& hash : new_hashes) reprs.push_back(util::Repr(hash));
  kLog.Debug("New report hashes: [" + util::Join(reprs, ", ") + "]");
  return util::WriteFile(
      path, util::Join(std::vector<std::string>(all_hashes.begin(),
                                                all_hashes.end()),
                       "\n"));
}

}  // namespace cc::report
