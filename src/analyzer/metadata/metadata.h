// The metadata.json of a report folder, as `analyze` writes it (analyze.md
// §5).

#ifndef CC_ANALYZER_METADATA_METADATA_H_
#define CC_ANALYZER_METADATA_METADATA_H_

#include <yyjson.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cc::analyzer {

// The results of one analyzer (Python: "analyzer_statistics").
struct AnalyzerStatistics {
  int failed = 0;
  std::vector<std::string> failed_sources;
  int successful = 0;
  std::vector<std::string> successful_sources;
  std::string version;
};

// A checker and whether it was enabled.
struct CheckerStatus {
  std::string name;
  bool enabled = false;
};

// The checker states and results of one analyzer.
struct AnalyzerMetadata {
  std::vector<CheckerStatus> checkers;
  AnalyzerStatistics statistics;
};

// When the analysis began and ended, in seconds since the epoch.
struct Timestamps {
  double begin = 0;
  double end = 0;
};

// The entry of `analyze` in "tools" (Python: metadata_tool).
struct MetadataTool {
  int action_num = 0;
  std::vector<std::string> command;
  std::string version;
  std::string working_directory;
  std::string output_path;
  // Result file -> analyzed source file, in insertion order.
  std::vector<std::pair<std::string, std::string>> result_source_files;
  std::vector<std::pair<std::string, AnalyzerMetadata>> analyzers;
  std::optional<std::string> run_name;
  std::optional<int> skipped;
  std::optional<Timestamps> timestamps;
};

// Sets `result_file` -> `source` in the result_source_files of `tool`,
// keeping the place of an existing key (Python: dict assignment).
void SetResultSource(const std::string& result_file, const std::string& source,
                     MetadataTool* tool);

// Removes `result_file` from the result_source_files of `tool`, if it is
// there.
void RemoveResultSource(const std::string& result_file, MetadataTool* tool);

// Returns the "result_source_files" of the metadata.json document
// `metadata`, of the version 1 layout or of every tool of the version 2
// layout (Python: __get_result_source_files()).
std::vector<std::pair<std::string, std::string>> ResultSourceFiles(
    yyjson_val* metadata);

// Returns ResultSourceFiles() of the metadata.json at `path`; nullopt if
// the file cannot be read.
std::optional<std::vector<std::pair<std::string, std::string>>>
ReadResultSourceFiles(const std::string& path);

// Returns the metadata.json text of `tool` (Python: json.dump()).
std::string MetadataJson(const MetadataTool& tool);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_METADATA_METADATA_H_
