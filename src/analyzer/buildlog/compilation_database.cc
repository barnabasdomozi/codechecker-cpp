#include "analyzer/buildlog/compilation_database.h"

#include <yyjson.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/util.h"
#include "platform/filesystem.h"
#include "util/json.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/shlex.h"

namespace cc::analyzer {
namespace {

using Database = Result<std::optional<util::MutableJsonDocument>>;

constexpr std::string_view kCompilationDatabase = "compile_commands.json";

// The extensions of C, C++ and Objective-C files (Python:
// C_CPP_OBJC_OBJCPP_EXTS).
constexpr std::string_view kSourceExtensions[] = {
    ".c",  ".i",  ".ii", ".m",  ".mi",  ".mm",  ".M",  ".mii",
    ".cc", ".cp", ".cxx", ".cpp", ".CPP", "c++", ".C",  ".hh",
    ".H",  ".hp", ".hxx", ".hpp", ".HPP", ".h++", ".tcc"};

// Python's exception for a database of the wrong shape: printed like the
// last line of the traceback.
Database Crash(std::string_view message) {
  util::Print(message);
  return Database(Error{});
}

std::string StringMember(yyjson_val* entry, std::string_view key) {
  return util::JsonToStr(util::JsonGet(entry, key));
}

// Returns the compile_commands.json files of the folders above `path`,
// closest first, down to "/".
std::vector<std::string> ParentDatabases(const std::string& path) {
  std::vector<std::string> databases;
  std::string folder = util::pypath::Abspath(path);
  while (true) {
    folder = util::pypath::Dirname(folder);
    const std::string database =
        util::pypath::Join({folder, kCompilationDatabase});
    if (util::pypath::IsFile(database)) databases.push_back(database);
    if (folder == "/") break;
  }
  return databases;
}

// Returns the database whose folder is the closest parent of `source`
// (Python: __select_compilation_database()).
std::optional<std::string> SelectDatabase(
    const std::vector<std::string>& databases, const std::string& source) {
  std::vector<std::string> folders;
  for (const std::string& database : databases) {
    folders.push_back(util::pypath::Dirname(database));
  }
  std::vector<std::string> paths = folders;
  paths.push_back(source);
  const std::optional<std::string> longest = util::pypath::Commonpath(paths);
  if (!longest || std::ranges::find(folders, *longest) == folders.end()) {
    return std::nullopt;
  }
  return util::pypath::Join({*longest, kCompilationDatabase});
}

}  // namespace

Database GatherCompilationDatabase(const std::string& input) {
  util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  if (std::optional<util::JsonDocument> json =
          common::LoadJson(input, /*display_warning=*/false);
      json && !yyjson_is_null(yyjson_doc_get_root(json->get()))) {
    // Case 1: a compilation database file.
    yyjson_mut_doc_set_root(
        doc, yyjson_val_mut_copy(doc, yyjson_doc_get_root(json->get())));
  } else if (util::pypath::IsFile(input) &&
             std::ranges::find(kSourceExtensions,
                               util::pypath::Splitext(input).second) !=
                 std::end(kSourceExtensions)) {
    // Case 2: a source file, described by the closest database.
    yyjson_mut_val* entries = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, entries);
    const std::vector<std::string> databases = ParentDatabases(input);
    if (!databases.empty()) {
      const std::optional<util::JsonDocument> database =
          common::LoadJson(databases.front());
      yyjson_val* root =
          database ? yyjson_doc_get_root(database->get()) : nullptr;
      if (!yyjson_is_arr(root)) {
        return Crash("TypeError: 'NoneType' object is not iterable");
      }
      const std::string source = util::pypath::Abspath(input);
      yyjson_arr_iter iter = yyjson_arr_iter_with(root);
      while (yyjson_val* entry = yyjson_arr_iter_next(&iter)) {
        if (util::pypath::Abspath(util::pypath::Join(
                {StringMember(entry, "directory"),
                 StringMember(entry, "file")})) == source) {
          yyjson_mut_arr_append(entries, yyjson_val_mut_copy(doc, entry));
        }
      }
    }
  } else if (util::pypath::IsDir(input)) {
    // Case 3: a folder, with the databases in and above it.
    yyjson_mut_val* entries = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, entries);
    std::vector<std::string> databases;
    for (const std::string& file : platform::WalkFiles(input)) {
      if (util::pypath::Basename(file) == kCompilationDatabase) {
        databases.push_back(file);
      }
    }
    for (const std::string& database : ParentDatabases(input)) {
      databases.push_back(database);
    }
    for (const std::string& database_file : databases) {
      const std::optional<util::JsonDocument> database =
          common::LoadJson(database_file);
      if (!database) continue;
      yyjson_arr_iter iter =
          yyjson_arr_iter_with(yyjson_doc_get_root(database->get()));
      while (yyjson_val* entry = yyjson_arr_iter_next(&iter)) {
        const std::string file = util::pypath::Join(
            {StringMember(entry, "directory"), StringMember(entry, "file")});
        if (util::pypath::Commonpath({file, input}) == input &&
            SelectDatabase(databases, file) == database_file) {
          yyjson_mut_arr_append(entries, yyjson_val_mut_copy(doc, entry));
        }
      }
    }
  } else {
    return Database(std::nullopt);
  }

  // Entries of intercept-build have "arguments" instead of "command".
  yyjson_mut_val* root = yyjson_mut_doc_get_root(doc);
  if (yyjson_mut_is_obj(root)) {
    if (yyjson_mut_obj_size(root) == 0) return Database(std::move(document));
    return Crash("TypeError: string indices must be integers, not 'str'");
  }
  if (!yyjson_mut_is_arr(root)) {
    return Crash("TypeError: the compilation database is not iterable");
  }
  yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(root);
  while (yyjson_mut_val* entry = yyjson_mut_arr_iter_next(&iter)) {
    if (!yyjson_mut_is_obj(entry)) {
      return Crash("TypeError: argument of type is not iterable");
    }
    if (yyjson_mut_obj_get(entry, "command") != nullptr) continue;
    yyjson_mut_val* arguments = yyjson_mut_obj_get(entry, "arguments");
    if (arguments == nullptr) return Crash("KeyError: 'arguments'");
    std::vector<std::string> args;
    yyjson_mut_arr_iter arg_iter = yyjson_mut_arr_iter_with(arguments);
    while (yyjson_mut_val* arg = yyjson_mut_arr_iter_next(&arg_iter)) {
      args.push_back(util::JsonString(arg));
    }
    const std::string command = util::ShlexJoin(args);
    yyjson_mut_obj_remove_key(entry, "arguments");
    yyjson_mut_obj_add_strncpy(doc, entry, "command", command.data(),
                               command.size());
  }
  return Database(std::move(document));
}

}  // namespace cc::analyzer
