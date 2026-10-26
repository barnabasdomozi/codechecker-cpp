// Port of codechecker_report_converter/report/statistics.py: the summary
// tables that `parse` prints after the reports (parse.md §4.3).

#ifndef CC_REPORT_OUTPUT_STATISTICS_H_
#define CC_REPORT_OUTPUT_STATISTICS_H_

#include <stddef.h>

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "report/model/report.h"

namespace cc::report {

// Counts the result files and the reports by severity, checker and file,
// each in the order first seen. The const methods can be called from
// several threads at the same time; the others need exclusive access.
class Statistics {
 public:
  Statistics() = default;
  Statistics(const Statistics& other) = default;
  Statistics& operator=(const Statistics& other) = default;

  // Counts one processed result file.
  void AddResultFile() { ++num_of_analyzer_result_files_; }

  // Counts `report` (Python: add_report()).
  void AddReport(const Report& report);

  int num_of_reports() const { return num_of_reports_; }

  // Prints the tables on stdout: those of severities, checkers and files
  // if there are reports, and the summary (Python: write()).
  void Write() const;

 private:
  // A severity or a file, with the number of its reports.
  struct ReportCount {
    std::string name;
    int count = 0;
  };

  // A checker and the severity of its reports, with their number.
  struct CheckerCount {
    std::string name;
    std::string severity;
    int count = 0;
  };

  int num_of_analyzer_result_files_ = 0;
  int num_of_reports_ = 0;
  std::vector<ReportCount> severities_;
  std::vector<CheckerCount> checkers_;
  // (name, severity) -> index in checkers_.
  std::map<std::pair<std::string, std::string>, size_t> checker_index_;
  // Named by original path; the table shows the base names.
  std::vector<ReportCount> files_;
  std::unordered_map<std::string, size_t> file_index_;  // Index in files_.
};

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_STATISTICS_H_
