// Port of codechecker_analyzer/compilation_database.py: the compilation
// database of the input of `analyze` (analyze.md §3).

#ifndef CC_ANALYZER_BUILDLOG_COMPILATION_DATABASE_H_
#define CC_ANALYZER_BUILDLOG_COMPILATION_DATABASE_H_

#include <optional>
#include <string>

#include "util/json.h"
#include "util/result.h"

namespace cc::analyzer {

// Returns the compilation database that describes `input` (Python:
// gather_compilation_database()): the JSON file itself, the entries of the
// closest compile_commands.json of a C/C++/Objective-C source file, or the
// entries of the files of a folder from the compile_commands.json files in
// and above it. nullopt if `input` is none of these. Entries with
// "arguments" and no "command" get a "command". A database of the wrong
// shape is an error, reported like Python's exception.
Result<std::optional<util::MutableJsonDocument>> GatherCompilationDatabase(
    const std::string& input);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_BUILDLOG_COMPILATION_DATABASE_H_
