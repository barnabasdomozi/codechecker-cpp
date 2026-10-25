#include "common/version.h"

#include <yyjson.h>

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "common/util.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/result.h"

namespace cc::common {
namespace {

// Returns the string member `key` of `object`, or nullopt if `object` has no
// such string member.
std::optional<std::string> StringMember(yyjson_val* object, const char* key) {
  yyjson_val* value = yyjson_obj_get(object, key);
  if (!yyjson_is_str(value)) return std::nullopt;
  return std::string(yyjson_get_str(value), yyjson_get_len(value));
}

// Returns a string member that every version file has. The build writes the
// version files, so a missing one is an internal error, as the KeyError
// traceback it causes in Python.
std::string RequiredMember(yyjson_val* object, const char* key) {
  std::optional<std::string> value = StringMember(object, key);
  if (!value) std::terminate();
  return std::move(*value);
}

// Returns whether Python's `not value` is true: for null, false, 0, "", []
// and {}.
bool IsFalsy(yyjson_val* value) {
  switch (yyjson_get_type(value)) {
    case YYJSON_TYPE_NULL:
      return true;
    case YYJSON_TYPE_BOOL:
      return !yyjson_get_bool(value);
    case YYJSON_TYPE_NUM:
      return yyjson_get_num(value) == 0;
    case YYJSON_TYPE_STR:
      return yyjson_get_len(value) == 0;
    case YYJSON_TYPE_ARR:
      return yyjson_arr_size(value) == 0;
    case YYJSON_TYPE_OBJ:
      return yyjson_obj_size(value) == 0;
    default:
      // A raw number, too big for 64 bits, so not 0.
      return false;
  }
}

}  // namespace

Result<VersionInfo> ReadVersionFile(const std::string& path) {
  const std::optional<util::JsonDocument> document = LoadJson(path);
  if (!document || IsFalsy(yyjson_doc_get_root(document->get()))) {
    return Result<VersionInfo>(Error{});
  }
  yyjson_val* root = yyjson_doc_get_root(document->get());
  yyjson_val* version = yyjson_obj_get(root, "version");
  VersionInfo info;
  info.version = RequiredMember(version, "major") + "." +
                 RequiredMember(version, "minor") + "." +
                 RequiredMember(version, "revision");
  const std::optional<std::string> rc = StringMember(version, "rc");
  if (rc && !rc->empty()) info.version += "-rc" + *rc;
  info.build_date = RequiredMember(root, "package_build_date");
  info.git_hash = StringMember(root, "git_hash");
  const util::LogLevel level = util::CurrentLogLevel();
  const bool debug = level == util::LogLevel::kDebug ||
                     level == util::LogLevel::kDebugAnalyzer;
  info.git_tag = StringMember(yyjson_obj_get(root, "git_describe"),
                              debug ? "dirty" : "tag");
  return Result<VersionInfo>(std::move(info));
}

}  // namespace cc::common
