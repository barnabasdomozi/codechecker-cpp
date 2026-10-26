#include "analyzer/drivers/result_handler.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analyzer/drivers/analyzer.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/output/error_file.h"
#include "report/output/plist.h"
#include "util/pypath.h"
#include "util/result.h"

namespace cc::analyzer {

std::string SourceDirPath(const AnalyzerRun& run) {
  return util::pypath::Abspath(run.action->directory);
}

void DropSkippedReports(const common::SkipListHandlers* skip_handlers,
                        std::vector<report::Report>* reports) {
  if (skip_handlers == nullptr || skip_handlers->empty()) return;
  std::erase_if(*reports, [skip_handlers](const report::Report& report) {
    return common::ShouldSkip(*skip_handlers, report.file.original_path);
  });
}

report::HashType ReportHashType(const std::optional<std::string>& option) {
  if (option == "context-free-v2") return report::HashType::kContextFree;
  if (option == "diagnostic-message") {
    return report::HashType::kDiagnosticMessage;
  }
  return report::HashType::kPathSensitive;
}

void SetReportHashes(const AnalyzerRun& run,
                     std::vector<report::Report>* reports) {
  const report::HashType hash_type = ReportHashType(run.config->report_hash);
  for (report::Report& report : *reports) {
    report.report_hash = report::GetReportHash(report, hash_type);
  }
}

Result<void> WriteReports(const AnalyzerRun& run,
                          std::vector<report::Report> reports,
                          std::string_view analyzer_name,
                          const common::ReviewStatusHandler* review_status) {
  if (review_status != nullptr) {
    std::erase_if(reports, [review_status](const report::Report& report) {
      return review_status->ShouldIgnore(report);
    });
  }
  return report::WritePlistFile(run.result_file, reports,
                                std::string(analyzer_name));
}

Result<void> UpdateErrorFile(const AnalyzerRun& run,
                             std::string_view analyzer_name) {
  return report::UpdateErrorFile(run.result_file, run.return_code,
                                 analyzer_name, run.analyzer_cmd, run.out,
                                 run.err);
}

Result<void> WriteResultFiles(
    const AnalyzerRun& run, std::vector<report::Report> reports,
    std::string_view analyzer_name,
    const common::ReviewStatusHandler* review_status) {
  if (Result<void> written = WriteReports(run, std::move(reports),
                                          analyzer_name, review_status);
      !written.ok()) {
    return written;
  }
  return UpdateErrorFile(run, analyzer_name);
}

}  // namespace cc::analyzer
