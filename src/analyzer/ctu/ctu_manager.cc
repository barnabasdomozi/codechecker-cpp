#include "analyzer/ctu/ctu_manager.h"

#include <yyjson.h>

#include <cstdio>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "analyzer/context/run_command.h"
#include "analyzer/ctu/ctu_triple_arch.h"
#include "platform/filesystem.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/yaml.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("analyzer");

bool StartsWithDigit(std::string_view text) {
  return !text.empty() && text[0] >= '0' && text[0] <= '9';
}

// Splits a function map line into the function's name and its path: the new
// format (Clang 16) is "<length>:<usr> <path>", the old one "<usr> <path>".
std::pair<std::string, std::string> SplitMapLine(std::string_view line) {
  if (StartsWithDigit(line)) {
    const size_t colon = line.find(':');
    const std::string_view length_text = line.substr(0, colon);
    const size_t length =
        static_cast<size_t>(util::ParseInt(length_text).value_or(0));
    const size_t name_end =
        colon == std::string_view::npos
            ? line.size()
            : colon + 1 +
                  util::CodePointPrefixSize(line.substr(colon + 1), length);
    const std::string_view rest =
        name_end + 1 <= line.size() ? line.substr(name_end + 1) : "";
    return {std::string(line.substr(0, name_end)), std::string(rest)};
  }
  const size_t space = line.find(' ');
  if (space == std::string_view::npos) return {std::string(line), ""};
  return {std::string(line.substr(0, space)),
          std::string(line.substr(space + 1))};
}

}  // namespace

void MergeFunctionMaps(const std::string& func_map_dir,
                       const std::string& output_file) {
  // Function name -> the files that define it, in the order seen.
  std::vector<std::pair<std::string, std::vector<std::string>>> functions;
  std::map<std::string, size_t> index;
  for (const std::string& name : platform::ListDirectory(func_map_dir)) {
    if (name.starts_with('.')) continue;
    const Result<std::string> content =
        util::ReadFile(util::pypath::Join({func_map_dir, name}));
    if (!content.ok()) continue;
    const std::string text =
        util::TranslateNewlines(util::DecodeUtf8Ignore(content.value()));
    for (const std::string_view raw : util::SplitLines(text)) {
      const std::string_view line = util::Strip(raw);
      if (line.empty()) continue;
      auto [function, file] = SplitMapLine(line);
      const auto [it, inserted] = index.emplace(function, functions.size());
      if (inserted) functions.emplace_back(std::move(function),
                                           std::vector<std::string>());
      std::vector<std::string>& files = functions[it->second].second;
      if (std::ranges::find(files, file) == files.end()) {
        files.push_back(std::move(file));
      }
    }
  }
  std::string output;
  for (const auto& [function, files] : functions) {
    // Functions defined in several files are left out of CTU.
    if (files.size() == 1) output += function + " " + files[0] + "\n";
  }
  (void)util::WriteFile(output_file, output);
}

void MergeExtdefMappings(const std::string& ctu_dir,
                         const std::string& map_file) {
  for (const std::string& arch : platform::ListDirectory(ctu_dir)) {
    const std::string arch_dir = util::pypath::Join({ctu_dir, arch});
    if (arch.starts_with('.') || !util::pypath::IsDir(arch_dir)) continue;
    const std::string fnmap_dir =
        util::pypath::Join({arch_dir, kCtuTempFnMapFolder});
    MergeFunctionMaps(fnmap_dir, util::pypath::Join({arch_dir, map_file}));
    platform::RemoveTree(fnmap_dir);
  }
}

std::vector<std::string> AstCommand(const BuildAction& action,
                                    const AnalyzerConfig& config,
                                    const std::string& triple_arch,
                                    const std::string& source,
                                    std::string* ast_dir) {
  const std::string real_source = util::pypath::Realpath(source);
  const std::string ast_path = util::pypath::Abspath(util::pypath::Join(
      {config.ctu_dir, triple_arch, "ast", real_source.substr(1) + ".ast"}));
  *ast_dir = util::pypath::Dirname(ast_path);
  std::vector<std::string> command = CtuCompileCommand(action, config, source);
  // __clang_analyzer__ must be set in the imported translation units too.
  command.insert(command.end(),
                 {"-emit-ast", "-D__clang_analyzer__", "-w", "-o", ast_path});
  return command;
}

void GenerateAst(const std::string& triple_arch, const BuildAction& action,
                 const std::string& source, const AnalyzerConfig& config) {
  std::string ast_dir;
  const std::vector<std::string> command =
      AstCommand(action, config, triple_arch, source, &ast_dir);
  // Errors are ignored, as in Python; the AST command then fails.
  (void)platform::EnsureDirs(ast_dir);
  const std::string text = util::Join(command, " ");
  kLog.DebugAnalyzer("Generating AST using '" + text + "'");
  const Result<CommandOutput> output =
      RunProc(command, {.cwd = action.directory});
  if (!output.ok() || output.value().return_code != 0) {
    kLog.Error(std::format("Error generating AST.\n\ncommand:\n\n{}\n\nstderr:"
                           "\n\n{}",
                           text,
                           output.ok() ? output.value().err
                                       : output.error().message));
  }
}

void GenerateInvocationList(const std::string& triple_arch,
                            const BuildAction& action,
                            const std::string& source,
                            const AnalyzerConfig& config) {
  const std::string arch_dir =
      util::pypath::Join({config.ctu_dir, triple_arch});
  if (const std::error_code error = platform::EnsureDirs(arch_dir)) {
    kLog.Error(util::OsError(error.value(), arch_dir).message);
    return;
  }
  std::vector<std::string> command = CtuCompileCommand(action, config, source);
  command.insert(command.end(), {"-D__clang_analyzer__", "-w"});
  // One "<source>:\n- <arg>\n..." mapping entry, with no line folding, as
  // LLVM's YAML parser needs; the key is a simple key whatever its length.
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  const std::string source_path = util::pypath::Realpath(source);
  yyjson_mut_val* key_list = yyjson_mut_arr(doc);
  yyjson_mut_arr_add_strncpy(doc, key_list, source_path.data(),
                             source_path.size());
  yyjson_mut_val* args = yyjson_mut_arr(doc);
  for (const std::string& arg : command) {
    yyjson_mut_arr_add_strncpy(doc, args, arg.data(), arg.size());
  }
  constexpr int kWidth = std::numeric_limits<int>::max();
  std::string key = util::DumpYaml(key_list, kWidth);  // "- <key>\n"
  key = key.substr(2, key.size() - 3);
  const std::string line = key + ":\n" + util::DumpYaml(args, kWidth);
  kLog.Debug("Appending invocation list item '" + line + "'");
  std::FILE* file = std::fopen(
      util::pypath::Join({arch_dir, "invocation-list.yml"}).c_str(), "ab");
  if (file == nullptr) return;
  std::fwrite(line.data(), 1, line.size(), file);
  std::fclose(file);
}

std::vector<std::string> ExtdefMappingCommand(const BuildAction& action,
                                              const AnalyzerConfig& config,
                                              const std::string& source,
                                              const std::string& tool) {
  std::vector<std::string> command = CtuCompileCommand(action, config);
  command[0] = tool;
  command.insert(command.begin() + 1, {source, "--"});
  return command;
}

void MapFunctions(const std::string& triple_arch, const BuildAction& action,
                  const std::string& source, const AnalyzerConfig& config,
                  const std::string& tool) {
  const std::vector<std::string> command =
      ExtdefMappingCommand(action, config, source, tool);
  const std::string text = util::Join(command, " ");
  kLog.DebugAnalyzer("Generating function map using '" + text + "'");
  const Result<CommandOutput> output =
      RunProc(command, {.cwd = action.directory});
  if (!output.ok() || output.value().return_code != 0) {
    kLog.Error(std::format(
        "Error generating function map.\n\ncommand:\n\n{}\n\nstderr:\n\n{}",
        text, output.ok() ? output.value().err : output.error().message));
    return;
  }
  // On-demand CTU maps to the sources, AST-dump CTU to the AST files.
  std::string mapped;
  for (const std::string_view line : util::StrSplitLines(output.value().out)) {
    auto [function, path] = SplitMapLine(line);
    if (!config.ctu_on_demand) {
      const std::string_view relative =
          std::string_view(path).substr(path.starts_with('/') ? 1 : 0);
      path = util::pypath::Join({"ast", std::string(relative) + ".ast"});
    }
    mapped += function + " " + path + "\n";
  }
  const std::string folder =
      util::pypath::Join({config.ctu_dir, triple_arch, kCtuTempFnMapFolder});
  // Errors are ignored, as in Python; writing the map then fails.
  (void)platform::EnsureDirs(folder);
  if (mapped.empty()) return;
  if (const std::optional<std::string> file =
          platform::MakeTempFile(folder, "")) {
    (void)util::WriteFile(*file, mapped);
  }
}

}  // namespace cc::analyzer
