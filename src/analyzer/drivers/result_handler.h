// The steps that the result handlers of the analyzers share (Python: the
// postprocess_result() methods of the ResultHandler subclasses;
// analyzer-drivers.md §6).

#ifndef CC_ANALYZER_DRIVERS_RESULT_HANDLER_H_
#define CC_ANALYZER_DRIVERS_RESULT_HANDLER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analyzer/drivers/analyzer.h"
#include "common/review_status_handler.h"
#include "common/skiplist_handler.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "util/result.h"

namespace cc::analyzer {

// Returns the folder of the build action, absolute (Python:
// ResultHandler.source_dir_path).
std::string SourceDirPath(const AnalyzerRun& run);

// Drops the reports whose file `skip_handlers` skip (Python: Report.skip()).
void DropSkippedReports(const common::SkipListHandlers* skip_handlers,
                        std::vector<report::Report>* reports);

// Returns the report hash that --report-hash `option` selects for the
// analyzers other than Clang SA: path-sensitive by default and for
// "context-free".
report::HashType ReportHashType(const std::optional<std::string>& option);

// Gives each of `reports` the report hash of ReportHashType() for the
// --report-hash option of `run`.
void SetReportHashes(const AnalyzerRun& run,
                     std::vector<report::Report>* reports);

// Drops the reports that `review_status` ignores and writes the others into
// the result file of `run`, with `analyzer_name` in its metadata (Python:
// report_file.create()).
Result<void> WriteReports(const AnalyzerRun& run,
                          std::vector<report::Report> reports,
                          std::string_view analyzer_name,
                          const common::ReviewStatusHandler* review_status);

// Python: error_file.update() for `run`.
Result<void> UpdateErrorFile(const AnalyzerRun& run,
                             std::string_view analyzer_name);

// WriteReports(), then UpdateErrorFile(): the end of postprocess_result()
// of the analyzers other than Clang SA.
Result<void> WriteResultFiles(const AnalyzerRun& run,
                              std::vector<report::Report> reports,
                              std::string_view analyzer_name,
                              const common::ReviewStatusHandler* review_status);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_DRIVERS_RESULT_HANDLER_H_
