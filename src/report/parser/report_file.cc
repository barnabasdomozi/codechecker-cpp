#include "report/parser/report_file.h"

#include <string>
#include <utility>
#include <vector>

#include "platform/filesystem.h"
#include "report/model/report.h"
#include "report/parser/plist.h"
#include "report/parser/sarif.h"
#include "util/logging.h"
#include "util/pypath.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

}  // namespace

bool IsSupported(const std::string& path) {
  return path.ends_with(".plist") || path.ends_with(".sarif");
}

std::vector<ResultFolder> AnalyzerResultFiles(
    const std::vector<std::string>& inputs) {
  std::vector<ResultFolder> folders;
  for (const std::string& input : inputs) {
    const std::string path = util::pypath::Abspath(input);
    kLog.Debug("Parsing input argument: '" + path + "'");
    if (util::pypath::IsFile(path)) {
      if (IsSupported(path)) {
        folders.push_back(
            {.path = util::pypath::Dirname(path), .files = {path}});
      }
    } else if (util::pypath::IsDir(path)) {
      for (platform::WalkedFolder& folder : platform::Walk(path)) {
        ResultFolder result = {.path = std::move(folder.path), .files = {}};
        for (std::string& file : folder.files) {
          if (IsSupported(file)) result.files.push_back(std::move(file));
        }
        folders.push_back(std::move(result));
      }
    }
  }
  return folders;
}

std::vector<Report> GetReports(const std::string& path) {
  // Python checks the end of the name without the dot.
  if (path.ends_with("plist")) return ReadPlistFile(path);
  if (path.ends_with("sarif")) return ReadSarifFile(path);
  return {};
}

}  // namespace cc::report
