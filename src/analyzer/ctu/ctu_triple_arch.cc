#include "analyzer/ctu/ctu_triple_arch.h"

#include <algorithm>
#include <string>
#include <vector>

#include "analyzer/context/run_command.h"
#include "analyzer/drivers/clangsa.h"
#include "analyzer/drivers/flag.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::analyzer {

std::vector<std::string> CtuCompileCommand(const BuildAction& action,
                                           const AnalyzerConfig& config,
                                           const std::string& source,
                                           const std::string& output) {
  std::vector<std::string> command = {ClangSaAnalyzer().Binary().value_or("")};
  if (!HasFlag("--target", command) && !action.target.empty()) {
    command.push_back("--target=" + action.target);
  }
  command.push_back("-c");
  if (!HasFlag("-x", command)) {
    command.insert(command.end(), {"-x", action.lang});
  }
  command.insert(command.end(), config.analyzer_extra_arguments.begin(),
                 config.analyzer_extra_arguments.end());
  command.insert(command.end(), action.analyzer_options.begin(),
                 action.analyzer_options.end());
  // The implicit include folders come last, so the user's own folders win.
  AppendPrependedAll(
      config.add_gcc_include_dirs_with_isystem ? "-isystem" : "-idirafter",
      action.compiler_includes, &command);
  if (!output.empty()) command.insert(command.end(), {"-o", output});
  if (!source.empty()) command.push_back(source);
  // Python appends the standard even when it is empty.
  if (!HasFlag("-std", command) && !HasFlag("--std", command)) {
    command.push_back(action.compiler_standard);
  }
  return command;
}

std::string TripleArch(const BuildAction& action, const std::string& source,
                       const AnalyzerConfig& config) {
  std::vector<std::string> command =
      CtuCompileCommand(action, config, source);
  command.insert(command.begin() + 1, "-###");
  const Result<CommandOutput> output =
      RunProc(command, {.cwd = action.directory});
  if (!output.ok()) return "";
  // The -### output lists the commands of the compilation steps; quotes are
  // stripped, which is enough to find -triple.
  std::vector<std::string> words =
      util::SplitWhitespace(output.value().out + output.value().err);
  for (std::string& word : words) {
    const size_t start = word.find_first_not_of('"');
    const size_t end = word.find_last_not_of('"');
    word =
        start == std::string::npos ? "" : word.substr(start, end - start + 1);
  }
  const auto triple = std::ranges::find(words, "-triple");
  if (triple == words.end() || triple + 1 == words.end()) return "";
  const std::string& arch = *(triple + 1);
  return arch.substr(0, arch.find('-'));
}

}  // namespace cc::analyzer
