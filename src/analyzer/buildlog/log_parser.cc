#include "analyzer/buildlog/log_parser.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analyzer/buildlog/build_action.h"
#include "analyzer/drivers/clangsa.h"
#include "common/skiplist_handler.h"
#include "common/util.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "util/file.h"
#include "util/fnmatch.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/memo.h"
#include "util/parallel.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/shlex.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::analyzer {
namespace {

constexpr util::Logger kLog("buildlogger");

// The extensions of the sources listed in response files.
constexpr std::string_view kSourceExtensions[] = {
    ".c", ".cc", ".cp", ".cpp", ".cxx", ".c++", ".o", ".so", ".a"};

// GCC flags replaced by the ones Clang knows.
constexpr std::pair<std::string_view, std::string_view> kReplacements[] = {
    {"-mips32", "-target mips -mips32"},
    {"-mips64", "-target mips64 -mips64"},
    {"-mpowerpc", "-target powerpc"},
    {"-mpowerpc64", "-target powerpc64"}};

// The flags skipped with a number of parameters (Python:
// IGNORED_PARAM_OPTIONS).
constexpr std::pair<std::string_view, int> kIgnoredParamOptions[] = {
    {"-install_name", 1}, {"-exported_symbols_list", 1},
    {"-current_version", 1}, {"-compatibility_version", 1}, {"-init$", 1},
    {"-e$", 1}, {"-seg1addr", 1}, {"-bundle_loader", 1},
    {"-multiply_defined", 1}, {"-sectorder", 3}, {"--param$", 1},
    {"-u$", 1}, {"--serialize-diagnostics", 1}, {"-framework", 1},
    {"-filelist", 1}};

constexpr std::string_view kXclangFlagsToSkip[] = {
    "-module-file-info", "-S",          "-emit-llvm",         "-emit-llvm-bc",
    "-emit-llvm-only",   "-emit-llvm-uselists", "-rewrite-objc"};

// The include-like flags whose parameter is a path (Python:
// flags_with_path).
constexpr std::string_view kFlagsWithPath[] = {
    "-I",        "-idirafter", "-iquote",  "-isysroot", "-isystem",
    "-sysroot",  "--sysroot",  "-include", "--include", "-imacros",
    "-iprefix",  "-iwithprefix", "-iwithprefixbefore"};

const util::Regex& IgnoredOptionsClang() {
  static const util::Regex& regex =
      *new util::Regex("-Werror|-pedantic-errors|-w");
  return regex;
}

const util::Regex& CompileOptions() {
  static const util::Regex& regex = *new util::Regex(
      R"(-nostdinc|-nostdinc\+\+|-pedantic|-O[1-3]|-Os|-{1,2}std=|)"
      R"(-{1,2}stdlib=|-{1,2}target=|-f|-m|-Wno-|--sysroot=|-sdkroot|)"
      R"(--gcc-toolchain=|-pthread)");
  return regex;
}

const util::Regex& CompileOptionsMerged() {
  static const util::Regex& regex = *new util::Regex(
      "(--sysroot|-sdkroot|--include|-include|-iquote|-[DIUF]|-idirafter|"
      "-isystem|-imacros|-isysroot|-iprefix|-iwithprefix|"
      "-iwithprefixbefore)");
  return regex;
}

const util::Regex& IncludeOptionsMerged() {
  static const util::Regex& regex = *new util::Regex(
      "(-iquote|-[IF]|-isystem|-iprefix|-iwithprefix|-iwithprefixbefore)");
  return regex;
}

const util::Regex& PrecompilationOption() {
  static const util::Regex& regex =
      *new util::Regex("-(E|M[G|T|Q|F|J|P|V|M]*)$");
  return regex;
}

// The parsed fields of an entry while its arguments are processed (Python:
// the `details` dictionary of parse_options()).
struct Details {
  std::vector<std::string> analyzer_options;
  std::vector<std::string> compiler_includes;
  std::string compiler_standard;
  std::string compilation_target;
  std::string original_command;
  std::string directory;
  std::string output;
  std::optional<std::string> lang;
  std::string arch;
  std::string target;
  std::string compiler;
  std::optional<ActionType> action_type;
};

// The arguments of a command with a position (Python: OptionIterator).
// Next() fails at the end, where Python raises StopIteration.
class OptionIterator {
 public:
  explicit OptionIterator(const std::vector<std::string>* args) : args_(args) {}

  OptionIterator(const OptionIterator&) = delete;
  OptionIterator& operator=(const OptionIterator&) = delete;

  const std::string& item() const { return (*args_)[position_]; }
  bool AtEnd() const { return position_ >= args_->size(); }
  // Moves to the next argument; false at the end.
  bool Next() {
    ++position_;
    return position_ < args_->size();
  }

 private:
  const std::vector<std::string>* args_;
  size_t position_ = 1;  // The compiler is not an option.
};

// A flag processor: returns whether it handled the current argument, or an
// error that drops the entry (Python's StopIteration).
using Processor = Result<bool> (*)(OptionIterator* it, Details* details);

Result<bool> Handled() { return Result<bool>(true); }
Result<bool> NotHandled() { return Result<bool>(false); }
Result<bool> StopIteration() { return Result<bool>(Error{.message = ""}); }

Result<bool> SkipGcc(OptionIterator* it, Details* /*details*/) {
  if (IgnoredOptionsGcc().Match(it->item())) return Handled();
  static const auto& kPatterns = *[] {
    auto* patterns = new std::vector<std::pair<util::Regex, int>>;
    for (const auto& [pattern, count] : kIgnoredParamOptions) {
      patterns->emplace_back(util::Regex(pattern), count);
    }
    return patterns;
  }();
  for (const auto& [regex, count] : kPatterns) {
    if (!regex.Match(it->item())) continue;
    for (int i = 0; i < count; ++i) {
      if (!it->Next()) return StopIteration();
    }
    return Handled();
  }
  return NotHandled();
}

Result<bool> Replace(OptionIterator* it, Details* details) {
  for (const auto& [flag, replacement] : kReplacements) {
    if (it->item() != flag) continue;
    for (std::string& part : util::Split(replacement, " ")) {
      details->analyzer_options.push_back(std::move(part));
    }
    return Handled();
  }
  return NotHandled();
}

Result<bool> CollectTransformIncludeOpts(OptionIterator* it,
                                         Details* details) {
  const std::optional<util::RegexMatch> match =
      CompileOptionsMerged().Match(it->item());
  if (!match) return NotHandled();
  const std::string flag(*match->Group(0));
  bool together = flag.size() != it->item().size();
  std::string param;
  if (together) {
    param = it->item().substr(flag.size());
  } else {
    if (!it->Next()) return StopIteration();
    param = it->item();
  }
  // The .plist files need absolute paths, which Clang prints only if the
  // include paths are absolute.
  if (std::ranges::find(kFlagsWithPath, flag) != std::end(kFlagsWithPath)) {
    if (param.empty() && flag.find("sysroot") == std::string::npos) {
      return Result<bool>(Error{.message = "string index out of range"});
    }
    if (flag.find("sysroot") != std::string::npos || param[0] != '=') {
      // --sysroot=/path is written as --sysroot /path, also a valid form.
      if (!param.empty() && param[0] == '=') {
        param.erase(0, 1);
        together = false;
      }
      param = util::pypath::Normpath(
          util::pypath::Join({details->directory, param}));
    }
  }
  if (together) {
    details->analyzer_options.push_back(flag + param);
  } else {
    details->analyzer_options.push_back(flag);
    details->analyzer_options.push_back(param);
  }
  return Handled();
}

Result<bool> CollectCompileOpts(OptionIterator* it, Details* details) {
  if (!CompileOptions().Match(it->item())) return NotHandled();
  details->analyzer_options.push_back(it->item());
  return Handled();
}

Result<bool> SkipSources(OptionIterator* it, Details* /*details*/) {
  if (it->item().empty()) {
    return Result<bool>(Error{.message = "string index out of range"});
  }
  return Result<bool>(it->item()[0] != '-');
}

Result<bool> DetermineActionType(OptionIterator* it, Details* details) {
  const std::string& item = it->item();
  if (item == "-c") {
    details->action_type = ActionType::kCompile;
    return Handled();
  }
  const bool info = item.starts_with("-print-prog-name");
  if (!info && !PrecompilationOption().Match(item)) return NotHandled();
  if (details->action_type != ActionType::kCompile) {
    details->action_type = info ? ActionType::kInfo : ActionType::kPreprocess;
  }
  return Handled();
}

Result<bool> GetArch(OptionIterator* it, Details* details) {
  if (it->item() != "-arch") return NotHandled();
  if (!it->Next()) return StopIteration();
  details->arch = it->item();
  return Handled();
}

Result<bool> GetTarget(OptionIterator* it, Details* details) {
  if (it->item() != "--target" && it->item() != "-target") {
    return NotHandled();
  }
  if (!it->Next()) return StopIteration();
  details->compilation_target = it->item();
  return Handled();
}

Result<bool> GetLanguage(OptionIterator* it, Details* details) {
  if (!it->item().starts_with("-x")) return NotHandled();
  if (it->item() == "-x") {
    if (!it->Next()) return StopIteration();
    details->lang = it->item();
  } else {
    details->lang = it->item().substr(2);
  }
  return Handled();
}

Result<bool> GetOutput(OptionIterator* it, Details* details) {
  if (it->item() != "-o") return NotHandled();
  if (!it->Next()) return StopIteration();
  details->output = it->item();
  return Handled();
}

Result<bool> SkipClang(OptionIterator* it, Details* /*details*/) {
  return Result<bool>(IgnoredOptionsClang().Match(it->item()).has_value());
}

Result<bool> CollectTransformXclangOpts(OptionIterator* it, Details* details) {
  if (it->item() != "-Xclang") return NotHandled();
  if (!it->Next()) return StopIteration();
  if (std::ranges::find(kXclangFlagsToSkip, it->item()) ==
      std::end(kXclangFlagsToSkip)) {
    details->analyzer_options.push_back("-Xclang");
    details->analyzer_options.push_back(it->item());
  }
  return Handled();
}

Result<bool> CollectClangCompileOpts(OptionIterator* it, Details* details) {
  details->analyzer_options.push_back(it->item());
  return Handled();
}

constexpr Processor kGccProcessors[] = {
    SkipGcc,     Replace,     CollectTransformIncludeOpts, CollectCompileOpts,
    DetermineActionType, SkipSources, GetArch, GetTarget, GetLanguage,
    GetOutput};

constexpr Processor kClangProcessors[] = {
    SkipSources, SkipClang,   CollectTransformXclangOpts,
    GetOutput,   DetermineActionType, GetArch, GetTarget, GetLanguage,
    CollectTransformIncludeOpts, CollectClangCompileOpts};

// Python: get_language().
std::optional<std::string> LanguageOfExtension(std::string_view extension) {
  constexpr std::pair<std::string_view, std::string_view> kLanguages[] = {
      {".c", "c"},    {".cp", "c++"},  {".cpp", "c++"}, {".cxx", "c++"},
      {".txx", "c++"}, {".cc", "c++"}, {".C", "c++"},   {".ii", "c++"},
      {".m", "objective-c"}, {".mm", "objective-c++"}};
  for (const auto& [ext, lang] : kLanguages) {
    if (ext == extension) return std::string(lang);
  }
  return std::nullopt;
}

// Returns the folder of a Clang compiler, or nullopt if `compiler` is not
// Clang (Python: __get_installed_dir()).
std::optional<std::string> ClangInstalledDir(const std::string& compiler) {
  const std::string resolved = util::pypath::Realpath(compiler);
  if (util::pypath::Basename(resolved).find("clang") == std::string::npos) {
    return std::nullopt;
  }
  return util::pypath::Dirname(resolved);
}

// Returns whether `folder` has no *intrin.h headers (Python:
// __contains_no_intrinsic_headers()).
bool ContainsNoIntrinsicHeaders(const std::string& folder) {
  static auto& kCache = *new util::Memo<std::string, bool>();
  return kCache.Get(folder, [&folder] {
    if (!util::pypath::Exists(folder)) return true;
    for (const std::string& name : platform::ListDirectory(folder)) {
      if (!name.starts_with('.') && util::FnMatch(name, "*intrin.h")) {
        return false;
      }
    }
    return true;
  });
}

// Returns the flags that change the implicit include paths (Python:
// filter_compiler_includes_extra_args()).
std::vector<std::string> CompilerInfoFlags(
    const std::vector<std::string>& options) {
  static const util::Regex& kPattern = *new util::Regex(
      "-m(32|64)|-{1,2}std=|-{1,2}stdlib=|-nostdinc|--driver-mode=");
  std::vector<std::string> flags;
  for (const std::string& option : options) {
    if (kPattern.Match(option)) flags.push_back(option);
  }
  for (size_t i = 0; i < options.size(); ++i) {
    if (!options[i].starts_with("--sysroot")) continue;
    if (options[i] != "--sysroot") {
      flags.push_back(options[i]);
    } else if (i + 1 < options.size()) {
      flags.push_back("--sysroot=" + options[i + 1]);
    }
    break;
  }
  return flags;
}

// Runs a compiler command to read its stderr (Python:
// __get_compiler_err()); nullopt if it cannot start.
std::optional<std::string> CompilerErr(
    const std::vector<std::string>& command) {
  std::vector<std::string> env = platform::CurrentEnvironment();
  platform::SetEnv(&env, "LC_ALL", "C");
  const platform::RunResult result = platform::RunProcess(
      command, {.env = env, .input = ""});
  if (result.start_error != 0) {
    kLog.Error(std::format("Error during process execution: {}\n{}\n",
                           util::ShlexJoin(command),
                           std::strerror(result.start_error)));
    return std::nullopt;
  }
  return util::TranslateNewlines(util::DecodeUtf8Ignore(result.err));
}

// Python: get_compiler_includes().
std::vector<std::string> CompilerIncludes(const CompilerInfoKey& key) {
  std::vector<std::string> command = {key.compiler};
  command.insert(command.end(), key.flags.begin(), key.flags.end());
  command.insert(command.end(), {"-E", "-x", key.language, "-", "-v"});
  kLog.Debug("Retrieving default includes via " + util::ShlexJoin(command));
  std::vector<std::string> includes;
  const std::optional<std::string> err = CompilerErr(command);
  if (!err) return includes;
  bool inside = false;
  for (const std::string_view raw : util::StrSplitLines(*err)) {
    if (raw.starts_with("End of search list.")) break;
    if (inside) {
      std::string line(util::Strip(raw));
      const size_t framework = line.find("(framework directory)");
      if (framework != std::string::npos) {
        line = framework == 0 ? line.substr(0, line.size() - 1)
                              : line.substr(0, framework - 1);
      }
      includes.push_back(util::pypath::Normpath(line));
    }
    if (raw.starts_with("#include <...> search starts here:")) inside = true;
  }
  return includes;
}

// Python: get_compiler_target().
std::string CompilerTarget(const std::string& compiler) {
  const std::optional<std::string> err = CompilerErr({compiler, "-v"});
  std::string target;
  if (!err) return target;
  for (const std::string_view line : util::SplitLines(*err)) {
    const std::vector<std::string> parts = util::SplitWhitespace(line);
    if (parts.size() > 1 && parts[0] == "Target:") target = parts[1];
  }
  return target;
}

// Python: get_compiler_standard(): the GNU variant of the default standard.
std::string CompilerStandard(const std::string& compiler,
                             const std::string& language) {
  static constexpr std::string_view kVersionC = R"(
#ifdef __STDC_VERSION__
#  if __STDC_VERSION__ >= 201710L
#    error CC_FOUND_STANDARD_VER#17
#  elif __STDC_VERSION__ >= 201112L
#    error CC_FOUND_STANDARD_VER#11
#  elif __STDC_VERSION__ >= 199901L
#    error CC_FOUND_STANDARD_VER#99
#  elif __STDC_VERSION__ >= 199409L
#    error CC_FOUND_STANDARD_VER#94
#  else
#    error CC_FOUND_STANDARD_VER#90
#  endif
#else
#  error CC_FOUND_STANDARD_VER#90
#endif
        )";
  static constexpr std::string_view kVersionCpp = R"(
#ifdef __cplusplus
#  if __cplusplus >= 201703L
#    error CC_FOUND_STANDARD_VER#17
#  elif __cplusplus >= 201402L
#    error CC_FOUND_STANDARD_VER#14
#  elif __cplusplus >= 201103L
#    error CC_FOUND_STANDARD_VER#11
#  elif __cplusplus >= 199711L
#    error CC_FOUND_STANDARD_VER#98
#  else
#    error CC_FOUND_STANDARD_VER#98
#  endif
#else
#  error CC_FOUND_STANDARD_VER#98
#endif
        )";
  const bool c = language == "c";
  const std::optional<std::string> source =
      platform::MakeTempFile(platform::TempDir(), c ? ".c" : ".cpp");
  if (!source) return "";
  std::string standard;
  if (util::WriteFile(*source, c ? kVersionC : kVersionCpp).ok()) {
    if (const std::optional<std::string> err =
            CompilerErr({compiler, *source})) {
      static const util::Regex& kFound =
          *new util::Regex("CC_FOUND_STANDARD_VER#(.+)");
      if (const std::optional<util::RegexMatch> match = kFound.Search(*err)) {
        standard = std::string(*match->Group(1));
      }
    }
  }
  std::remove(source->c_str());
  if (standard.empty()) return "";
  if (standard == "94") return "-std=iso9899:199409";
  return std::string("-std=gnu") + (c ? "" : "++") + standard;
}

// Returns the JSON text of `key` as compiler_info.json uses it: the list of
// the compiler, the language and the flags.
std::string KeyText(const CompilerInfoKey& key) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* list = yyjson_mut_arr(doc);
  yyjson_mut_arr_add_strncpy(doc, list, key.compiler.data(),
                             key.compiler.size());
  yyjson_mut_arr_add_strncpy(doc, list, key.language.data(),
                             key.language.size());
  yyjson_mut_val* flags = yyjson_mut_arr_add_arr(doc, list);
  for (const std::string& flag : key.flags) {
    yyjson_mut_arr_add_strncpy(doc, flags, flag.data(), flag.size());
  }
  return util::DumpJson(list);
}

// Python: process_response_file(): the options of a response file, and the
// sources among them.
Result<std::pair<std::vector<std::string>, std::vector<std::string>>>
ProcessResponseFile(const std::string& path) {
  using Parsed =
      Result<std::pair<std::vector<std::string>, std::vector<std::string>>>;
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) return Parsed(content.error());
  Result<std::vector<std::string>> options =
      util::ShlexSplit(util::DecodeUtf8Ignore(content.value()));
  if (!options.ok()) return Parsed(options.error());
  std::vector<std::string> sources;
  for (const std::string& option : options.value()) {
    if (option.starts_with('-')) continue;
    const std::string extension =
        util::AsciiLower(util::pypath::Splitext(option).second);
    if (std::ranges::find(kSourceExtensions, extension) !=
        std::end(kSourceExtensions)) {
      sources.push_back(option);
    }
  }
  return Parsed({std::move(options).value(), std::move(sources)});
}

// Returns the entries to parse, with response files expanded (Python:
// extend_compilation_database_entries()). The commands of the original
// entries are changed in place. An entry that is not an object is an
// error.
Result<std::vector<yyjson_mut_val*>> ExtendEntries(
    yyjson_mut_doc* doc, yyjson_mut_val* database) {
  using Entries = Result<std::vector<yyjson_mut_val*>>;
  std::vector<yyjson_mut_val*> entries;
  yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(database);
  while (yyjson_mut_val* entry = yyjson_mut_arr_iter_next(&iter)) {
    if (!yyjson_mut_is_obj(entry)) {
      return Entries(Error{.message = "TypeError"});
    }
    yyjson_mut_val* command = yyjson_mut_obj_get(entry, "command");
    if (!yyjson_mut_is_str(command) ||
        util::JsonString(command).find('@') == std::string::npos) {
      entries.push_back(entry);
      continue;
    }
    yyjson_mut_val* directory = yyjson_mut_obj_get(entry, "directory");
    if (!yyjson_mut_is_str(directory)) {
      return Entries(Error{.message = "KeyError: 'directory'"});
    }
    const std::string source_dir = util::JsonString(directory);
    Result<std::vector<std::string>> options =
        util::ShlexSplit(util::JsonString(command));
    if (!options.ok()) return Entries(options.error());
    std::vector<std::string> new_command;
    std::vector<std::string> source_files;
    std::set<std::string> response_files;
    for (const std::string& option : options.value()) {
      if (!option.starts_with('@')) {
        new_command.push_back(util::ShlexQuote(option));
        continue;
      }
      const std::string response_file =
          util::pypath::Join({source_dir, option.substr(1)});
      if (util::pypath::Exists(response_file)) {
        auto parsed = ProcessResponseFile(response_file);
        if (!parsed.ok()) return Entries(parsed.error());
        for (const std::string& item : parsed.value().first) {
          new_command.push_back(util::ShlexQuote(item));
        }
        source_files.insert(source_files.end(), parsed.value().second.begin(),
                            parsed.value().second.end());
        response_files.insert(util::pypath::Abspath(response_file));
      } else if (util::pypath::Exists(
                     util::pypath::Join({source_dir, option}))) {
        new_command.push_back(util::ShlexQuote(option));
      } else {
        kLog.Warning("Response file '" + response_file + "' does not exists.");
      }
    }
    const std::string joined = util::Join(new_command, " ");
    yyjson_mut_obj_put(entry, yyjson_mut_str(doc, "command"),
                       yyjson_mut_strncpy(doc, joined.data(), joined.size()));
    yyjson_mut_val* file = yyjson_mut_obj_get(entry, "file");
    const std::string file_name = util::JsonString(file);
    if (file_name.starts_with('@') &&
        response_files.contains(util::pypath::Abspath(
            util::pypath::Join({source_dir, file_name.substr(1)})))) {
      // One entry for each source of the response file.
      for (const std::string& source : source_files) {
        yyjson_mut_val* copy = yyjson_mut_val_mut_copy(doc, entry);
        yyjson_mut_obj_put(
            copy, yyjson_mut_str(doc, "file"),
            yyjson_mut_strncpy(doc, source.data(), source.size()));
        entries.push_back(copy);
      }
      continue;
    }
    entries.push_back(entry);
  }
  return Entries(std::move(entries));
}

// Returns the arguments of the compile command of `entry`, and sets the
// original command and the directory in `details`. The error message is
// that of the exception that drops the entry in Python.
Result<std::vector<std::string>> EntryCommand(yyjson_mut_val* entry,
                                              Details* details) {
  using Command = Result<std::vector<std::string>>;
  std::vector<std::string> command;
  yyjson_mut_val* arguments = yyjson_mut_obj_get(entry, "arguments");
  yyjson_mut_val* command_value = yyjson_mut_obj_get(entry, "command");
  if (arguments != nullptr) {
    yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(arguments);
    while (yyjson_mut_val* arg = yyjson_mut_arr_iter_next(&iter)) {
      command.push_back(util::JsonString(arg));
    }
    details->original_command = util::ShlexJoin(command);
  } else if (command_value != nullptr) {
    details->original_command = util::JsonString(command_value);
    Command split = util::ShlexSplit(details->original_command);
    if (!split.ok()) return split;
    command = std::move(split).value();
  } else {
    return Command(Error{
        .message = "\"No valid 'command' or 'arguments' entry found!\""});
  }
  yyjson_mut_val* directory = yyjson_mut_obj_get(entry, "directory");
  if (directory == nullptr) return Command(Error{.message = "'directory'"});
  details->directory = util::JsonString(directory);
  if (command.empty()) {
    return Command(Error{.message = "list index out of range"});
  }
  return Command(std::move(command));
}

// Sets the compiler of `command` in `details`, and C++ as the language if
// its name has "++" (Python: determine_compiler()). With "ccache g++ ...",
// the compiler is the second argument.
void DetermineCompiler(const std::vector<std::string>& command,
                       Details* details) {
  details->compiler = command[0];
  if (command[0].ends_with("ccache") && command.size() > 1 &&
      platform::Which(command[1], std::nullopt)) {
    details->compiler = command[1];
  }
  if (!details->compiler.starts_with('/') &&
      details->compiler.find('/') != std::string::npos) {
    details->compiler = util::pypath::Realpath(
        util::pypath::Join({util::pypath::Abspath(details->directory),
                            details->compiler}));
  }
  if (util::pypath::Basename(details->compiler).find("++") !=
      std::string::npos) {
    details->lang = "c++";
  }
}

// Returns whether `options` has a --gcc-toolchain (Python:
// gcc_toolchain.toolchain_in_args()).
bool HasGccToolchain(const std::vector<std::string>& options) {
  static const util::Regex& kToolchain =
      *new util::Regex("^--gcc-toolchain=(?P<tcpath>.*)$");
  return std::ranges::any_of(options, [](const std::string& option) {
    return option.find("--gcc-toolchain") != std::string::npos &&
           kToolchain.Match(option);
  });
}

// Fills the implicit include folders, standard and target in `details` that
// the command does not set, from `loaded_info` if it is set, else by
// asking the compiler through `compiler_info`.
void AddImplicitCompilerInfo(
    const std::optional<std::vector<std::pair<CompilerInfoKey, CompilerInfo>>>&
        loaded_info,
    ImplicitCompilerInfo* compiler_info, Details* details) {
  std::vector<std::string> flags = CompilerInfoFlags(details->analyzer_options);
  std::ranges::sort(flags);
  const CompilerInfoKey key = {.compiler = details->compiler,
                               .language = *details->lang,
                               .flags = std::move(flags)};
  std::optional<CompilerInfo> info;
  if (loaded_info) {
    const auto found = std::ranges::find(
        *loaded_info, key, &std::pair<CompilerInfoKey, CompilerInfo>::first);
    if (found != loaded_info->end()) info = found->second;
  } else {
    info = compiler_info->Get(key);
  }
  if (!info) return;
  if (details->compiler_includes.empty()) {
    details->compiler_includes = info->compiler_includes;
  }
  if (details->compiler_standard.empty()) {
    details->compiler_standard = info->compiler_standard;
  }
  if (details->target.empty()) details->target = info->target;
}

// Drops the include-fixed folders of GCC from the implicit include folders,
// unless `options` keeps them, and the folders with intrinsic headers, also
// from the include options. Fails if an include option has no value.
Result<void> FilterGccIncludes(const LogParseOptions& options,
                               Details* details) {
  if (!options.keep_gcc_include_fixed) {
    std::erase_if(details->compiler_includes, [](const std::string& folder) {
      return util::pypath::Basename(util::pypath::Normpath(folder)) ==
             "include-fixed";
    });
  }
  if (options.keep_gcc_intrin) return Result<void>();
  std::erase_if(details->compiler_includes, [](const std::string& folder) {
    return !ContainsNoIntrinsicHeaders(folder);
  });
  std::vector<std::string> kept;
  const std::vector<std::string>& opts = details->analyzer_options;
  for (size_t i = 0; i < opts.size(); ++i) {
    const std::optional<util::RegexMatch> match =
        IncludeOptionsMerged().Match(opts[i]);
    if (!match) {
      kept.push_back(opts[i]);
      continue;
    }
    const std::string flag(*match->Group(0));
    const bool together = flag.size() != opts[i].size();
    std::string value;
    if (together) {
      value = opts[i].substr(flag.size());
    } else {
      if (i + 1 >= opts.size()) return Result<void>(Error{});
      value = opts[++i];
    }
    if (!util::pypath::IsDir(value) || ContainsNoIntrinsicHeaders(value)) {
      if (together) {
        kept.push_back(opts[i]);
      } else {
        kept.push_back(opts[i - 1]);
        kept.push_back(value);
      }
    }
  }
  details->analyzer_options = std::move(kept);
  return Result<void>();
}

// Python: parse_options(). Returns the error message of the exception that
// drops the entry.
Result<BuildAction> ParseEntry(
    yyjson_mut_val* entry, const LogParseOptions& options,
    const std::optional<std::vector<std::pair<CompilerInfoKey, CompilerInfo>>>&
        loaded_info,
    ImplicitCompilerInfo* compiler_info) {
  Details details;
  Result<std::vector<std::string>> parsed_command =
      EntryCommand(entry, &details);
  if (!parsed_command.ok()) {
    return Result<BuildAction>(parsed_command.error());
  }
  const std::vector<std::string> command = std::move(parsed_command).value();
  DetermineCompiler(command, &details);

  std::span<const Processor> processors = kGccProcessors;
  bool same_clang = false;
  if (const std::optional<std::string> installed =
          ClangInstalledDir(details.compiler)) {
    processors = kClangProcessors;
    if (*installed ==
        util::pypath::Dirname(ClangSaAnalyzer().Binary().value_or(""))) {
      kLog.Debug("Same clang is used for compilation and analysis.");
      same_clang = true;
    }
  }
  OptionIterator it(&command);
  while (!it.AtEnd()) {
    for (const Processor processor : processors) {
      const Result<bool> handled = processor(&it, &details);
      if (!handled.ok()) return Result<BuildAction>(handled.error());
      if (handled.value()) break;
    }
    it.Next();
  }

  BuildAction action;
  action.action_type = details.action_type.value_or(ActionType::kCompile);
  yyjson_mut_val* file = yyjson_mut_obj_get(entry, "file");
  if (file == nullptr) return Result<BuildAction>(Error{.message = "'file'"});
  action.source = util::JsonString(file);
  if (!action.source.starts_with('/')) {
    action.source = util::pypath::Realpath(util::pypath::Join(
        {util::pypath::Abspath(details.directory), action.source}));
  }
  if (action.source == ".") action.source.clear();
  if (const std::optional<std::string> lang =
          LanguageOfExtension(util::pypath::Splitext(action.source).second)) {
    if (!details.lang) details.lang = *lang;
  } else {
    action.action_type = ActionType::kLink;
  }
  details.target = details.compilation_target;

  // Clang looks for the headers of a --gcc-toolchain itself.
  const bool toolchain = HasGccToolchain(details.analyzer_options);
  if (((!toolchain && !same_clang) || loaded_info) && details.lang &&
      !details.lang->empty()) {
    AddImplicitCompilerInfo(loaded_info, compiler_info, &details);
  }
  if (Result<void> filtered = FilterGccIncludes(options, &details);
      !filtered.ok()) {
    return Result<BuildAction>(filtered.error());
  }

  action.analyzer_options = std::move(details.analyzer_options);
  action.compiler_includes = std::move(details.compiler_includes);
  action.compiler_standard = std::move(details.compiler_standard);
  action.original_command = std::move(details.original_command);
  action.directory = std::move(details.directory);
  action.output = std::move(details.output);
  action.lang = details.lang.value_or("");
  action.target = std::move(details.target);
  action.arch = std::move(details.arch);
  return Result<BuildAction>(std::move(action));
}

}  // namespace

const util::Regex& IgnoredOptionsGcc() {
  static const util::Regex& regex = *new util::Regex(
      "-fallow-fetchr-insn|-fcall-saved-|-fcond-mismatch|-fconserve-stack|"
      "-fcrossjumping|-fcse-follow-jumps|-fcse-skip-blocks|"
      "-fcx-limited-range$|-fdump-rtl.*|-fext-.*-literals|-ffixed-r2|-ffp$|"
      "-mfp16-format|-mmitigate-rop|-fgcse-lm|-fhoist-adjacent-loads|"
      "-findirect-inlining|-finline-limit|-finline-local-initialisers|"
      "-fipa-sra|-fmacro-prefix-map|-fmerge-constants|"
      "-fno-aggressive-loop-optimizations|-f(no-)?allow-store-data-races|"
      "-fno-canonical-system-headers|-f(no-)?code-hoisting|"
      "-fno-delete-null-pointer-checks|-fno-defer-pop|"
      "-fno-extended-identifiers|-fno-freestanding|-fno-jump-table|"
      // A missing comma in the Python list joins these two.
      "-fno-keep-inline-dllexport-fno-keep-static-consts|"
      "-fno-lifetime-dse|-f(no-)?printf-return-value|"
      "-f(no-)?reorder-functions|-fno-strength-reduce|-fno-toplevel-reorder|"
      "-fno-unit-at-a-time|-fno-var-tracking-assignments|"
      "-fno-tree-dominator-opts|-fobjc-link-runtime|-fpartial-inlining|"
      "-fpeephole2|-fr$|-fregmove|-frename-registers|"
      "-frerun-cse-after-loop|-fs$|-fsanitize=bounds-strict|"
      "-fsched-pressure|-fsched-spec|-fstack-usage|-fstack-reuse|"
      "-fthread-jumps|-ftree-pre|-ftree-switch-conversion|-ftree-tail-merge|"
      "-m(no-)?abm|-m(no-)?sdata|-m(no-)?spe|-m(no-)?string$|-m(no-)?dsbt|"
      "-m(no-)?fixed-ssp|-m(no-)?pointers-to-nested-functions|"
      "-m(no-)?word-relocations|-mno-fp-ret-in-387|"
      "-mpreferred-stack-boundary|-mpcrel-func-addr|-mrecord-mcount$|"
      "-maccumulate-outgoing-args|-mcall-aixdesc|-mppa3-addr-bug|-mtraceback=|"
      "-mtext=|-misa=|-mfunction-return=|-mindirect-branch-register|"
      "-mindirect-branch=|-mfix-cortex-m3-ldrd$|-mmultiple$|-msahf$|"
      "-mskip-rax-setup$|-mthumb-interwork$|-mupdate$|-mapcs|"
      "-fno-merge-const-bfstores$|-fno-ipa-sra$|-mno-thumb-interwork$|"
      "-mno-sched-prolog|-DNDEBUG$|-save-temps|-Werror|-Wno-error|"
      "-pedantic-errors|-fprofile|-w|-g(.+)?$|-flto|-mxl|-mfloat-gprs|-mabi");
  return regex;
}

CompilerInfo ImplicitCompilerInfo::Get(const CompilerInfoKey& key) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::ranges::find(
        entries_, key, &std::pair<CompilerInfoKey, CompilerInfo>::first);
    if (found != entries_.end()) return found->second;
  }
  // Parallel parsers may compute the same key; the first result is kept.
  CompilerInfo info = {
      .compiler_includes = CompilerIncludes(key),
      .compiler_standard = CompilerStandard(key.compiler, key.language),
      .target = CompilerTarget(key.compiler)};
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::ranges::find(
      entries_, key, &std::pair<CompilerInfoKey, CompilerInfo>::first);
  if (found != entries_.end()) return found->second;
  entries_.emplace_back(key, info);
  return info;
}

std::string ImplicitCompilerInfo::Dump() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  for (const auto& [key, info] : entries_) {
    yyjson_mut_val* value = yyjson_mut_obj(doc);
    yyjson_mut_val* includes = yyjson_mut_arr(doc);
    for (const std::string& folder : info.compiler_includes) {
      yyjson_mut_arr_add_strncpy(doc, includes, folder.data(), folder.size());
    }
    yyjson_mut_obj_add_val(doc, value, "compiler_includes", includes);
    yyjson_mut_obj_add_strncpy(doc, value, "compiler_standard",
                               info.compiler_standard.data(),
                               info.compiler_standard.size());
    yyjson_mut_obj_add_strncpy(doc, value, "target", info.target.data(),
                               info.target.size());
    const std::string key_text = KeyText(key);
    yyjson_mut_obj_add(
        root, yyjson_mut_strncpy(doc, key_text.data(), key_text.size()), value);
  }
  return util::DumpJson(root);
}

std::vector<std::pair<CompilerInfoKey, CompilerInfo>> LoadCompilerInfo(
    const std::string& path) {
  std::vector<std::pair<CompilerInfoKey, CompilerInfo>> result;
  const std::optional<util::JsonDocument> document = common::LoadJson(path);
  if (!document) return result;
  yyjson_obj_iter iter =
      yyjson_obj_iter_with(yyjson_doc_get_root(document->get()));
  while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
    const Result<util::JsonDocument> parsed =
        util::ParseJson(util::JsonToStr(key));
    if (!parsed.ok()) continue;
    yyjson_val* list = yyjson_doc_get_root(parsed.value().get());
    CompilerInfoKey info_key = {
        .compiler = util::JsonToStr(yyjson_arr_get(list, 0)),
        .language = util::JsonToStr(yyjson_arr_get(list, 1)),
        .flags = {}};
    yyjson_arr_iter flags = yyjson_arr_iter_with(yyjson_arr_get(list, 2));
    while (yyjson_val* flag = yyjson_arr_iter_next(&flags)) {
      info_key.flags.push_back(util::JsonToStr(flag));
    }
    yyjson_val* value = yyjson_obj_iter_get_val(key);
    CompilerInfo info;
    yyjson_arr_iter includes = yyjson_arr_iter_with(
        util::JsonGet(value, "compiler_includes"));
    while (yyjson_val* folder = yyjson_arr_iter_next(&includes)) {
      info.compiler_includes.push_back(util::JsonToStr(folder));
    }
    if (yyjson_val* standard = util::JsonGet(value, "compiler_standard")) {
      info.compiler_standard = util::JsonToStr(standard);
    }
    if (yyjson_val* target = util::JsonGet(value, "target")) {
      info.target = util::JsonToStr(target);
    }
    result.emplace_back(std::move(info_key), std::move(info));
  }
  return result;
}

Result<ParsedLog> ParseUniqueLog(yyjson_mut_doc* document,
                                 yyjson_mut_val* compilation_database,
                                 const LogParseOptions& options,
                                 ImplicitCompilerInfo* compiler_info) {
  // Python catches ValueError, KeyError and TypeError of this phase.
  const auto invalid = [compilation_database]() {
    kLog.Error(yyjson_mut_arr_size(compilation_database) == 0
                   ? "The compile database is empty."
                   : "The compile database is not valid.");
    return Result<ParsedLog>(Error{});
  };
  std::optional<util::Regex> uniqueing_regex;
  const std::string& mode = options.compile_uniqueing;
  if (mode != "alpha" && mode != "none" && mode != "strict" &&
      mode != "symlink") {
    Result<util::Regex> regex = util::Regex::Compile(mode);
    if (!regex.ok()) {
      util::Print("re.PatternError: " + regex.error().message);
      return Result<ParsedLog>(Error{});
    }
    uniqueing_regex = std::move(regex).value();
  }
  if (options.jobs <= 0) return invalid();
  const Result<std::vector<yyjson_mut_val*>> entries =
      ExtendEntries(document, compilation_database);
  if (!entries.ok()) return invalid();

  std::optional<std::vector<std::pair<CompilerInfoKey, CompilerInfo>>>
      loaded_info;
  if (options.compiler_info_file &&
      util::pypath::Exists(*options.compiler_info_file)) {
    loaded_info = LoadCompilerInfo(*options.compiler_info_file);
  }
  std::vector<std::optional<Result<BuildAction>>> results(
      entries.value().size());
  util::ParallelFor(options.jobs, entries.value().size(), [&](size_t i) {
    results[i] = ParseEntry(entries.value()[i], options, loaded_info,
                            compiler_info);
  });

  ParsedLog parsed;
  // The kept actions by their uniqueing key, in insertion order.
  std::vector<std::pair<std::string, BuildAction>> unique;
  std::map<std::string, size_t> index;
  for (std::optional<Result<BuildAction>>& result : results) {
    if (!result->ok()) {
      kLog.Error("Error processing entry: " + result->error().message);
      ++parsed.skipped;
      continue;
    }
    BuildAction action = std::move(*result).value();
    // With CTU or statistics, skipped files are still parsed for the
    // pre-analysis, unless that skips them too.
    if (options.analysis_skip_handlers != nullptr &&
        common::ShouldSkip(*options.analysis_skip_handlers, action.source) &&
        (!options.ctu_or_stats_enabled ||
         (options.pre_analysis_skip_handlers != nullptr &&
          common::ShouldSkip(*options.pre_analysis_skip_handlers,
                             action.source)))) {
      ++parsed.skipped;
      kLog.Debug("skipping: " + action.source);
      continue;
    }
    if (action.lang.empty() || action.action_type != ActionType::kCompile) {
      ++parsed.skipped;
      continue;
    }
    std::string key;
    if (mode == "none") {
      // Python: the hash of BuildAction and equality of the commands.
      key = util::Join(action.analyzer_options, "") + "-1" + action.target +
            action.source + '\0' + action.original_command;
    } else if (mode == "symlink") {
      key = util::pypath::Realpath(action.source);
    } else {
      key = action.source;
    }
    const auto known = index.find(key);
    if (known == index.end()) {
      index.emplace(key, unique.size());
      unique.emplace_back(key, std::move(action));
      continue;
    }
    BuildAction& kept = unique[known->second].second;
    if (mode == "strict") {
      kLog.Error(std::format("Build Action uniqueing failed as both '{}' and "
                             "'{}'",
                             kept.original_command, action.original_command));
      return Result<ParsedLog>(Error{});
    }
    if (mode == "alpha") {
      if (action.output < kept.output) kept = std::move(action);
    } else if (uniqueing_regex) {
      kLog.Debug("uniqueing regex");
      const bool new_matches =
          uniqueing_regex->Match(action.original_command).has_value();
      const bool kept_matches =
          uniqueing_regex->Match(kept.original_command).has_value();
      if (new_matches && !kept_matches) {
        kept = std::move(action);
      } else if (new_matches && kept_matches) {
        kLog.Error(std::format(
            "Build Action uniqueing failed as both \n {}\n and \n {} \n match "
            "regex pattern:{}",
            kept.original_command, action.original_command, mode));
        return Result<ParsedLog>(Error{});
      }
    }
  }
  for (auto& [key, action] : unique) {
    parsed.actions.push_back(std::move(action));
  }
  kLog.Debug("Parsing log file done.");
  return Result<ParsedLog>(std::move(parsed));
}

}  // namespace cc::analyzer
