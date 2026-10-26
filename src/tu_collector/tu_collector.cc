#include "tu_collector/tu_collector.h"

#include <yyjson.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/subprocess.h"
#include "util/fnmatch.h"
#include "util/json.h"
#include "util/parallel.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/zip.h"

namespace cc::tu_collector {
namespace {

// Removes the first argument after the compiler that starts with `option`,
// and its value if `has_arg` and the value is a separate argument, until
// there is none (Python: __eliminate_argument()).
void EliminateArgument(std::vector<std::string>* args, std::string_view option,
                       bool has_arg = false) {
  while (true) {
    size_t index = 0;
    while (index < args->size() && !(*args)[index].starts_with(option)) {
      ++index;
    }
    // Python stops at index 0 too, which is falsy.
    if (index == 0 || index == args->size()) return;
    const size_t separate =
        has_arg && (*args)[index].size() == option.size() ? 1 : 0;
    args->erase(args->begin() + static_cast<ptrdiff_t>(index),
                args->begin() + static_cast<ptrdiff_t>(
                                    std::min(index + separate + 1,
                                             args->size())));
  }
}

// Returns the files of the translation unit that `command` compiles, or the
// text of the error (Python: __gather_dependencies()).
Result<std::vector<std::string>> GatherDependencies(
    std::vector<std::string> command, const std::string& build_dir) {
  using Files = Result<std::vector<std::string>>;
  EliminateArgument(&command, "-o", /*has_arg=*/true);
  EliminateArgument(&command, "--output", /*has_arg=*/true);
  EliminateArgument(&command, "-specs");
  EliminateArgument(&command, "-MM");
  EliminateArgument(&command, "-MF", /*has_arg=*/true);
  EliminateArgument(&command, "-MP");
  EliminateArgument(&command, "-MT", /*has_arg=*/true);
  EliminateArgument(&command, "-MQ", /*has_arg=*/true);
  EliminateArgument(&command, "-MD");
  EliminateArgument(&command, "-MMD");
  EliminateArgument(&command, "-MJ", /*has_arg=*/true);
  EliminateArgument(&command, "-MV");
  // Python: __determine_compiler().
  size_t compiler_index = 0;
  if (command[0].ends_with("ccache") && command.size() > 1 &&
      platform::Which(command[1], std::nullopt)) {
    compiler_index = 1;
  }
  std::vector<std::string> dependency_command = {
      command[compiler_index], "-E", "-M", "-MT", "__dummy"};
  for (size_t i = compiler_index + 1; i < command.size(); ++i) {
    if (!command[i].empty()) dependency_command.push_back(command[i]);
  }
  EliminateArgument(&dependency_command, "--gcc-toolchain");

  const platform::RunResult run = platform::RunProcess(
      dependency_command,
      {.cwd = build_dir, .stderr_mode = platform::Output::kInherit});
  const std::string output = util::DecodeUtf8Replace(run.out);
  if (run.start_error != 0) {
    return Files(Error{.message = std::strerror(run.start_error)});
  }
  if (run.return_code != 0) return Files(Error{.message = output});
  // Python: the "Makefile" syntax of the output, read with str.replace().
  std::string dependencies;
  std::string_view rest = output;
  for (size_t pos = rest.find("__dummy: "); pos != std::string_view::npos;
       pos = rest.find("__dummy: ")) {
    dependencies.append(rest.substr(0, pos));
    rest.remove_prefix(pos + 9);
  }
  dependencies.append(rest);
  std::erase(dependencies, '\\');
  std::string collapsed;
  for (size_t i = 0; i < dependencies.size(); ++i) {
    if (dependencies[i] == ' ' && i + 1 < dependencies.size() &&
        dependencies[i + 1] == ' ') {
      ++i;  // Python: replace('  ', '').
      continue;
    }
    collapsed += dependencies[i] == ' ' ? '\n' : dependencies[i];
  }
  std::vector<std::string> files;
  for (const std::string_view line : util::StrSplitLines(collapsed)) {
    if (!line.empty()) files.push_back(util::pypath::Join({build_dir, line}));
  }
  return Files(std::move(files));
}

// Returns the compiler of a GCC toolchain that the command uses, or nullopt
// (Python: __get_toolchain_compiler()).
std::optional<std::string> ToolchainCompiler(
    const std::vector<std::string>& command) {
  for (const std::string& arg : command) {
    if (!arg.starts_with("--gcc-toolchain=")) continue;
    const bool is_cpp = command[0].find("++") != std::string::npos ||
                        command[0].find("cpp") != std::string::npos;
    return util::pypath::Join({arg.substr(16), "bin", is_cpp ? "g++" : "gcc"});
  }
  return std::nullopt;
}

}  // namespace

Dependencies GetDependentHeaders(const std::string& command,
                                 const std::string& build_dir) {
  Dependencies result;
  Result<std::vector<std::string>> args = util::ShlexSplit(command);
  if (!args.ok() || args.value().empty()) {
    result.error = args.ok() ? "" : args.error().message;
    return result;
  }
  std::vector<std::string> split = std::move(args).value();
  const auto gather = [&result, &build_dir](std::vector<std::string> cmd) {
    Result<std::vector<std::string>> files =
        GatherDependencies(std::move(cmd), build_dir);
    if (!files.ok()) {
      result.error += files.error().message;
      return;
    }
    result.files.insert(files.value().begin(), files.value().end());
  };
  gather(split);
  if (const std::optional<std::string> compiler = ToolchainCompiler(split)) {
    split[0] = *compiler;
    gather(split);
  }
  return result;
}

Result<void> AddSourcesToZip(const std::string& zip_file,
                             const std::set<std::string>& files) {
  Result<util::ZipArchive> archive =
      util::ZipArchive::Open(zip_file, util::ZipArchive::Mode::kAppend);
  if (!archive.ok()) return Result<void>(archive.error());
  for (const std::string& file : files) {
    const size_t start = file.find_first_not_of('/');
    const std::string archive_path = util::pypath::Normpath(util::pypath::Join(
        {"sources-root", start == std::string::npos
                             ? ""
                             : std::string_view(file).substr(start)}));
    if (archive.value().Contains(archive_path)) continue;
    if (Result<void> added = archive.value().AddFile(archive_path, file);
        !added.ok()) {
      return added;
    }
  }
  return archive.value().Close();
}

Result<void> ZipTuFiles(const std::string& zip_file,
                        const std::vector<CompileAction>& compilation_db) {
  std::set<std::string> tu_files;
  std::string error_messages;
  for (const CompileAction& action : compilation_db) {
    const Dependencies dependencies =
        GetDependentHeaders(action.command, action.directory);
    tu_files.insert(dependencies.files.begin(), dependencies.files.end());
    if (!dependencies.error.empty()) {
      error_messages += action.file + "\n" +
                        std::string(util::CodePointCount(action.file), '-') +
                        "\n" + dependencies.error + "\n";
    }
  }
  if (!error_messages.empty()) {
    Result<util::ZipArchive> archive =
        util::ZipArchive::Open(zip_file, util::ZipArchive::Mode::kWrite);
    if (!archive.ok()) return Result<void>(archive.error());
    if (Result<void> added = archive.value().AddText("no-sources",
                                                     error_messages);
        !added.ok()) {
      return added;
    }
    if (Result<void> closed = archive.value().Close(); !closed.ok()) {
      return closed;
    }
  } else {
    std::remove(zip_file.c_str());
  }
  if (Result<void> added = AddSourcesToZip(zip_file, tu_files); !added.ok()) {
    return added;
  }

  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* entries = yyjson_mut_arr(doc);
  for (const CompileAction& action : compilation_db) {
    yyjson_mut_val* entry = yyjson_mut_arr_add_obj(doc, entries);
    yyjson_mut_obj_add_strncpy(doc, entry, "file", action.file.data(),
                               action.file.size());
    yyjson_mut_obj_add_strncpy(doc, entry, "command", action.command.data(),
                               action.command.size());
    yyjson_mut_obj_add_strncpy(doc, entry, "directory",
                               action.directory.data(),
                               action.directory.size());
  }
  Result<util::ZipArchive> archive =
      util::ZipArchive::Open(zip_file, util::ZipArchive::Mode::kAppend);
  if (!archive.ok()) return Result<void>(archive.error());
  if (Result<void> added = archive.value().AddText(
          "compilation_database.json", util::DumpJsonIndent(entries, 2));
      !added.ok()) {
    return added;
  }
  return archive.value().Close();
}

std::set<std::string> GetDependentSources(
    const std::vector<CompileAction>& compilation_db,
    const std::vector<std::string>& header_paths, int jobs) {
  if (header_paths.empty()) return {};
  std::vector<std::set<std::string>> files(compilation_db.size());
  util::ParallelFor(jobs, compilation_db.size(), [&](size_t i) {
    files[i] = GetDependentHeaders(compilation_db[i].command,
                                   compilation_db[i].directory)
                   .files;
  });
  // Dependency -> the sources whose translation units read it.
  std::map<std::string, std::set<std::string>> dependencies;
  for (size_t i = 0; i < compilation_db.size(); ++i) {
    const std::string source = util::pypath::Join(
        {compilation_db[i].directory, compilation_db[i].file});
    for (const std::string& file : files[i]) dependencies[file].insert(source);
  }
  std::set<std::string> result;
  for (const std::string& header_path : header_paths) {
    std::optional<util::Regex> pattern;
    if (!header_path.empty()) {
      pattern = util::Regex(util::FnmatchTranslate(
          util::pypath::Normpath(util::Strip(header_path))));
    }
    for (const auto& [header, sources] : dependencies) {
      if (!pattern || pattern->Match(header)) {
        result.insert(sources.begin(), sources.end());
      }
    }
  }
  return result;
}

}  // namespace cc::tu_collector
