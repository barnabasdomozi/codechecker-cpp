// Port of codechecker_analyzer/analyzers/clangsa/ctu_manager.py and of
// codechecker_merge_clang_extdef_mappings: the data of the CTU collect
// phase (ctu-analysis.md §4).

#ifndef CC_ANALYZER_CTU_CTU_MANAGER_H_
#define CC_ANALYZER_CTU_CTU_MANAGER_H_

#include <string>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/analyzer.h"

namespace cc::analyzer {

// The folder of the function maps of each architecture, merged at the end.
inline constexpr char kCtuTempFnMapFolder[] = "tmpExternalFnMaps";

// Merges the function maps in `func_map_dir` into `output_file`, keeping the
// functions defined in exactly one file (Python: merge()).
void MergeFunctionMaps(const std::string& func_map_dir,
                       const std::string& output_file);

// Merges the function maps of every architecture folder of `ctu_dir` into
// `<arch>/<map_file>` and deletes the temporary maps (Python:
// merge_clang_extdef_mappings()).
void MergeExtdefMappings(const std::string& ctu_dir,
                         const std::string& map_file);

// Returns the command that writes the AST of `source`, and sets `ast_dir`
// to the folder of the AST file (Python: generate_ast_cmd()).
std::vector<std::string> AstCommand(const BuildAction& action,
                                    const AnalyzerConfig& config,
                                    const std::string& triple_arch,
                                    const std::string& source,
                                    std::string* ast_dir);

// Writes the AST file of `source` (Python: generate_ast()).
void GenerateAst(const std::string& triple_arch, const BuildAction& action,
                 const std::string& source, const AnalyzerConfig& config);

// Appends the compile command of `source` to the invocation list of its
// architecture, for on-demand CTU (Python: generate_invocation_list()).
void GenerateInvocationList(const std::string& triple_arch,
                            const BuildAction& action,
                            const std::string& source,
                            const AnalyzerConfig& config);

// Returns the command of the function mapping tool `tool` for `source`
// (Python: get_extdef_mapping_cmd()).
std::vector<std::string> ExtdefMappingCommand(const BuildAction& action,
                                              const AnalyzerConfig& config,
                                              const std::string& source,
                                              const std::string& tool);

// Writes the function map of `source` into a new temporary file of its
// architecture (Python: map_functions()).
void MapFunctions(const std::string& triple_arch, const BuildAction& action,
                  const std::string& source, const AnalyzerConfig& config,
                  const std::string& tool);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_CTU_CTU_MANAGER_H_
