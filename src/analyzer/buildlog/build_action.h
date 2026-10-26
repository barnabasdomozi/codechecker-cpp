// Port of codechecker_analyzer/buildlog/build_action.py: a compile command
// prepared for the analyzers (compiler-info.md §2, analyzer-drivers.md §1).

#ifndef CC_ANALYZER_BUILDLOG_BUILD_ACTION_H_
#define CC_ANALYZER_BUILDLOG_BUILD_ACTION_H_

#include <string>
#include <vector>

namespace cc::analyzer {

// What a compile command does; only kCompile commands are analyzed.
enum class ActionType { kLink, kCompile, kPreprocess, kInfo };

struct BuildAction {
  std::vector<std::string> analyzer_options;  // The kept compiler flags.
  std::vector<std::string> compiler_includes;  // Implicit include folders.
  std::string compiler_standard;  // The implicit -std=..., or empty.
  std::string analyzer_type;      // The analyzer of a job; empty before.
  std::string original_command;
  std::string directory;
  std::string output;  // The -o value.
  std::string lang;    // c, c++, ...; empty if unknown (Python: None).
  std::string target;  // The --target value.
  std::string source;  // The absolute source path.
  std::string arch;    // The -arch value.
  ActionType action_type = ActionType::kCompile;
};

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_BUILDLOG_BUILD_ACTION_H_
