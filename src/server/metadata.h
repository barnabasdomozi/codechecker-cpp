// Port of codechecker_server/metadata.py: what the server reads from the
// metadata.json of a stored report folder (store.md §4.3).

#ifndef CC_SERVER_METADATA_H_
#define CC_SERVER_METADATA_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api/ordered_map.h"

namespace cc::server {

// The analysis statistics of one analyzer.
struct AnalyzerStatistics {
  // The versions of the analyzer, joined by "; ".
  std::optional<std::string> version;
  // The source files whose analysis failed or succeeded, without
  // repetitions; nullopt if the metadata does not list them.
  std::optional<std::vector<std::string>> failed_sources;
  std::optional<std::vector<std::string>> successful_sources;
  // The number of successful analyses; nullopt if not given.
  std::optional<int64_t> successful;
};

// The content of a metadata.json (Python: MetadataInfoParser).
struct MetadataInfo {
  std::optional<std::string> cc_version;  // CodeChecker versions, "; ".
  std::vector<std::string> check_commands;
  std::vector<double> check_durations;  // Seconds.
  // In the order of the file, like the rest of the maps.
  cc::api::OrderedMap<std::string, AnalyzerStatistics> analyzer_statistics;
  // The checkers of each analyzer, and whether they were enabled.
  cc::api::OrderedMap<std::string, cc::api::OrderedMap<std::string, bool>>
      checkers;
  std::set<std::string> enabled_checkers;
  std::set<std::string> disabled_checkers;
  std::map<std::string, std::string> checker_to_analyzer;
};

// Returns the content of the metadata.json at `path`; empty if the file is
// missing, and if it cannot be read, which is logged.
MetadataInfo ParseMetadata(const std::string& path);

// Returns whether the checker `checker_name` is no longer available: it is
// not in the non-empty `enabled_checkers`, and it is not a clang
// diagnostic, which the metadata does not list (Python:
// checker_is_unavailable()).
bool CheckerIsUnavailable(const std::string& checker_name,
                          const std::set<std::string>& enabled_checkers);

}  // namespace cc::server

#endif  // CC_SERVER_METADATA_H_
