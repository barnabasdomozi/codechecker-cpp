// Port of codechecker_report_converter/report/hash.py: the report hashes
// that identify the same finding across analyses (report-hash.md).

#ifndef CC_REPORT_HASH_REPORT_HASH_H_
#define CC_REPORT_HASH_REPORT_HASH_H_

#include <string>

#include "report/model/report.h"

namespace cc::report {

enum class HashType {
  kContextFree,        // File name, message, source line without whitespace.
  kPathSensitive,      // Also the checker, and the last event and positions.
  kDiagnosticMessage,  // kContextFree and the messages of the events.
};

// Returns the report hash of `report` (Python: get_report_hash()). It reads
// source lines from disk and logs the problems it finds.
std::string GetReportHash(const Report& report, HashType type);

}  // namespace cc::report

#endif  // CC_REPORT_HASH_REPORT_HASH_H_
