// Port of codechecker_report_converter/report/hash.py: the report hashes
// that identify the same finding across analyses (report-hash.md).

#ifndef CC_REPORT_HASH_REPORT_HASH_H_
#define CC_REPORT_HASH_REPORT_HASH_H_

#include <string>

#include "report/model/report.h"
#include "report/model/source_file_cache.h"

namespace cc::report {

enum class HashType {
  kContextFree,        // File name, message, source line without whitespace.
  kPathSensitive,      // Also the checker, and the last event and positions.
  kDiagnosticMessage,  // kContextFree and the messages of the events.
};

// Returns the report hash of `report` (Python: get_report_hash()). It reads
// source lines through `sources` and logs the problems it finds.
std::string GetReportHash(const Report& report, HashType type,
                          SourceFileCache& sources);

// Returns the hash of the bug path of `report`, which is the same for
// duplicates of a report (Python: get_report_path_hash()). It uses the
// trimmed file paths.
std::string GetReportPathHash(const Report& report);

}  // namespace cc::report

#endif  // CC_REPORT_HASH_REPORT_HASH_H_
