// Port of tools/tu_collector/tu_collector/tu_collector.py: the files of a
// translation unit, collected with the compiler's dependency output
// (tu-collector.md).
//
// Inside CodeChecker these functions do not log: the Python version's
// tu_collector logger is disabled once CodeChecker sets up its logging.

#ifndef CC_TU_COLLECTOR_TU_COLLECTOR_H_
#define CC_TU_COLLECTOR_TU_COLLECTOR_H_

#include <set>
#include <string>
#include <vector>

#include "util/result.h"

namespace cc::tu_collector {

// An entry of a compilation database (Python: CompileAction).
struct CompileAction {
  std::string file;
  std::string command;
  std::string directory;
};

// The files of a translation unit (Python: the result of
// get_dependent_headers()).
struct Dependencies {
  std::set<std::string> files;
  // Why some files may be missing; empty if all were found.
  std::string error;
};

// Returns the files that the compile command `command`, run in `build_dir`,
// reads, by running the compiler with -E -M (Python:
// get_dependent_headers()). With --gcc-toolchain, the files of the
// toolchain's compiler are added.
Dependencies GetDependentHeaders(const std::string& command,
                                 const std::string& build_dir);

// Adds the files `files` to the ZIP file `zip_file` as
// "sources-root/<path>", unless they are in it already (Python:
// add_sources_to_zip()).
Result<void> AddSourcesToZip(const std::string& zip_file,
                             const std::set<std::string>& files);

// Writes the ZIP file `zip_file` with the files of the translation units of
// `compilation_db` and the database itself (Python: zip_tu_files() with its
// defaults). Files that could not be collected are explained in the entry
// "no-sources".
Result<void> ZipTuFiles(const std::string& zip_file,
                        const std::vector<CompileAction>& compilation_db);

// Returns the source files of `compilation_db` whose translation units
// include a file matching one of the shell patterns `header_paths` (Python:
// get_dependent_sources()). The compilers run in `jobs` threads.
std::set<std::string> GetDependentSources(
    const std::vector<CompileAction>& compilation_db,
    const std::vector<std::string>& header_paths, int jobs);

}  // namespace cc::tu_collector

#endif  // CC_TU_COLLECTOR_TU_COLLECTOR_H_
