#include "report/converter/result_file_formats.h"

#include <string>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "report/model/report.h"
#include "report/parser/plist.h"
#include "report/parser/sarif.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

}  // namespace

Result<std::vector<Report>> GetCppcheckReports(const std::string& path) {
  using Reports = std::vector<Report>;
  Reports reports;
  std::vector<std::string> plist_files;
  if (util::pypath::IsDir(path)) {
    // Python: glob.glob(os.path.join(path, "*.plist")), which skips hidden
    // files and keeps the order of the folder listing.
    for (const std::string& name : platform::ListDirectory(path)) {
      if (!name.starts_with('.') && name.ends_with(".plist")) {
        plist_files.push_back(util::pypath::Join({path, name}));
      }
    }
  } else if (util::pypath::IsFile(path) && path.ends_with(".plist")) {
    plist_files.push_back(path);
  } else {
    kLog.Error(
        "The given input should be an existing CppCheck result directory or "
        "a plist file.");
    return Result<Reports>(std::move(reports));
  }
  for (const std::string& plist_file : plist_files) {
    Reports plist_reports = ReadPlistFile(plist_file);
    for (Report& report : plist_reports) reports.push_back(std::move(report));
  }
  // The viewers show the message where the bug path ends.
  for (Report& report : reports) {
    const BugPathEvent last = {
        .message = report.message,
        .file = report.file,
        .line = report.line,
        .column = report.column,
        .range = Range{report.line, report.column, report.line,
                       report.column}};
    if (report.bug_path_events.empty() ||
        !(report.bug_path_events.back() == last)) {
      report.bug_path_events.push_back(last);
    }
  }
  return Result<Reports>(std::move(reports));
}

Result<std::vector<Report>> GetGccReports(const std::string& path) {
  return Result<std::vector<Report>>(ReadSarifFile(path));
}

}  // namespace cc::report
