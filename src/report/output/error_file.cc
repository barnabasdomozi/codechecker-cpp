#include "report/output/error_file.h"

#include <stdint.h>
#include <yyjson.h>

#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "util/file.h"
#include "util/json.h"
#include "util/plist.h"
#include "util/result.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

// Python: convert_to_printable().
std::string ToPrintable(std::string_view text) {
  std::string result;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t start = pos;
    const uint32_t c = util::NextCodePoint(text, &pos).value_or(0xFFFD);
    if (util::IsPrintable(c) || c == '\n' || c == '\r' || c == '\t') {
      result.append(text.substr(start, pos - start));
    } else {
      result += std::format("\\x{:02x}", c);
    }
  }
  return result;
}

}  // namespace

Result<void> UpdateErrorFile(const std::string& result_file, int return_code,
                             std::string_view analyzer_name,
                             const std::vector<std::string>& analyzer_cmd,
                             std::string_view out, std::string_view err) {
  const std::string error_file = result_file + ".err";
  std::remove(error_file.c_str());
  if (return_code == 0) return Result<void>();
  std::string command;
  for (const std::string& arg : analyzer_cmd) command += arg;
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  const auto add = [doc, root](const char* key, const std::string& value) {
    yyjson_mut_obj_add_strncpy(doc, root, key, value.data(), value.size());
  };
  add("analyzer_name", std::string(analyzer_name));
  add("analyzer_cmd", ToPrintable(command));
  yyjson_mut_obj_add_int(doc, root, "return_code", return_code);
  add("stdout", ToPrintable(out));
  add("stderr", ToPrintable(err));
  const Result<std::string> text = util::DumpPlist(root);
  if (!text.ok()) return Result<void>(text.error());
  return util::WriteFile(error_file, text.value());
}

}  // namespace cc::report
