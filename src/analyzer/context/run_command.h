// Running the analyzers and other tools of the analysis in their
// environment (Python: SourceAnalyzer.run_proc() and the
// subprocess.check_output() calls of the analyzer modules).

#ifndef CC_ANALYZER_CONTEXT_RUN_COMMAND_H_
#define CC_ANALYZER_CONTEXT_RUN_COMMAND_H_

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "platform/subprocess.h"
#include "util/result.h"

namespace cc::analyzer {

// The decoded output of a command.
struct CommandOutput {
  int return_code = 0;  // -N if signal N ended it.
  std::string out;
  std::string err;
};

// Options of RunProc() besides the command.
struct RunProcOptions {
  std::string cwd = {};
  // The environment; nullopt: the context's environment for the program.
  std::optional<Environment> env = std::nullopt;
  // Called with the pid if the command runs longer than this.
  std::optional<std::chrono::seconds> timeout = std::nullopt;
  std::function<void(int pid)> on_timeout = {};
};

// Runs `command` in a new process group and returns its output, decoded as
// UTF-8 with undecodable bytes dropped and universal newlines (Python:
// run_proc()). A command that cannot start is an error with the text of
// Python's OSError.
Result<CommandOutput> RunProc(const std::vector<std::string>& command,
                              const RunProcOptions& options);

// Returns the stdout of `command`, run in the context's environment for its
// program, decoded like RunProc() does (Python: subprocess.check_output()).
// `stderr_mode` is Output::kStdout to merge stderr into the result, or
// Output::kInherit to leave it on the program's stderr. A command that
// cannot start or exits with a non-zero code is an error with the text of
// Python's exception.
Result<std::string> CheckOutput(const std::vector<std::string>& command,
                                platform::Output stderr_mode);

}  // namespace cc::analyzer

#endif  // CC_ANALYZER_CONTEXT_RUN_COMMAND_H_
