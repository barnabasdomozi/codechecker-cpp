// Port of codechecker_analyzer/suppress_handler.py and
// suppress_file_handler.py: suppress files, which hide reports by their
// hash, and to which `parse --export-source-suppress` writes the review
// statuses of the source code comments (parse.md §6).

#ifndef CC_ANALYZER_SUPPRESS_HANDLER_H_
#define CC_ANALYZER_SUPPRESS_HANDLER_H_

#include <string>
#include <vector>

#include "report/model/report.h"
#include "util/result.h"

namespace cc::analyzer {

// One line of a suppress file.
struct SuppressEntry {
  std::string report_hash;
  std::string file_name;  // Empty in the old format.
  std::string comment;
  std::string status;
};

// The entries of a suppress file (Python: GenericSuppressHandler). The
// const methods can be called from several threads at the same time; the
// others need exclusive access.
class SuppressHandler {
 public:
  // Reads the suppress file at `path`; malformed lines are logged. With
  // `allow_write`, StoreSuppressBugId() writes to the file. `status_filter`
  // holds the review statuses that are shown; empty means all. Fails with
  // the text of Python's OSError if the file cannot be read.
  static Result<SuppressHandler> Create(std::string path, bool allow_write,
                                        std::vector<std::string> status_filter);

  SuppressHandler(const SuppressHandler& other) = default;
  SuppressHandler& operator=(const SuppressHandler& other) = default;

  // Returns whether `report` is hidden: an entry has its hash and its file
  // name, and a status that the filter does not show (Python:
  // get_suppressed()).
  bool IsSuppressed(const report::Report& report) const;

  // If writing is allowed, appends an entry to the file unless one with
  // `report_hash` and `file_name` (or no file name) is there, and reads the
  // file again (Python: store_suppress_bug_id()). Errors are logged.
  void StoreSuppressBugId(const std::string& report_hash,
                          const std::string& file_name,
                          const std::string& comment,
                          const std::string& status);

 private:
  SuppressHandler(std::string path, bool allow_write,
                  std::vector<std::string> status_filter,
                  std::vector<SuppressEntry> entries);

  std::string path_;
  bool allow_write_;
  std::vector<std::string> status_filter_;
  std::vector<SuppressEntry> entries_;
};

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_SUPPRESS_HANDLER_H_
