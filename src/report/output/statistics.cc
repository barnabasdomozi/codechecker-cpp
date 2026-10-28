#include "report/output/statistics.h"

#include <stddef.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "report/model/report.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/twodim.h"

namespace cc::report {
namespace {

using Rows = std::vector<std::vector<std::string>>;

// Writes one section: its title line, `table` and the closing line.
void WriteSection(std::string_view title, const std::string& table) {
  util::Write(util::Stream::kStdout, "\n" + std::string(title) + "\n" + table +
                                         "\n----=================----\n");
}

// Returns `rows` as a table under the header row that they start with.
std::string HeaderTable(const Rows& rows) {
  return util::twodim::ToTable(rows, util::twodim::TableStyle());
}

}  // namespace

void Statistics::AddReport(const Report& report) {
  ++num_of_reports_;
  // Python counts None severities under None, printed as "".
  const std::string severity = report.severity.value_or("");
  // There are only a few severities.
  const auto it = std::ranges::find(severities_, severity, &ReportCount::name);
  if (it != severities_.end()) {
    ++it->count;
  } else {
    severities_.push_back({.name = severity, .count = 1});
  }
  const auto [checker, new_checker] = checker_index_.try_emplace(
      {report.checker_name, severity}, checkers_.size());
  if (new_checker) {
    checkers_.push_back(
        {.name = report.checker_name, .severity = severity, .count = 0});
  }
  ++checkers_[checker->second].count;
  const auto [file, new_file] =
      file_index_.try_emplace(report.file.original_path, files_.size());
  if (new_file) {
    files_.push_back({.name = report.file.original_path, .count = 0});
  }
  ++files_[file->second].count;
}

void Statistics::Write() const {
  WriteReportTables();
  const Rows summary = {{"Number of processed analyzer result files",
                         std::to_string(num_of_analyzer_result_files_)},
                        {"Number of analyzer reports",
                         std::to_string(num_of_reports_)}};
  WriteSection("----======== Summary ========----",
               util::twodim::ToTable(summary, {.show_header = false,
                                               .rule_between_rows = true}));
}

void Statistics::WriteReportTables() const {
  if (!severities_.empty()) {
    Rows rows = {{"Severity", "Number of reports"}};
    for (const ReportCount& severity : severities_) {
      rows.push_back({severity.name, std::to_string(severity.count)});
    }
    WriteSection("----==== Severity Statistics ====----", HeaderTable(rows));
  }
  if (!checkers_.empty()) {
    Rows rows = {{"Checker name", "Severity", "Number of reports"}};
    for (const CheckerCount& checker : checkers_) {
      rows.push_back(
          {checker.name, checker.severity, std::to_string(checker.count)});
    }
    WriteSection("----==== Checker Statistics ====----", HeaderTable(rows));
  }
  if (!files_.empty()) {
    Rows rows = {{"File name", "Number of reports"}};
    for (const ReportCount& file : files_) {
      rows.push_back(
          {util::pypath::Basename(file.name), std::to_string(file.count)});
    }
    WriteSection("----==== File Statistics ====----", HeaderTable(rows));
  }
}

}  // namespace cc::report
