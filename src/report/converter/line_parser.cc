#include "report/converter/line_parser.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/file.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

}  // namespace

Result<std::vector<Report>> ParseLines(const std::string& path,
                                       const ParseLineFunction& parse_line) {
  using Reports = std::vector<Report>;
  Reports reports;
  if (!util::pypath::Exists(path)) {
    kLog.Error("Result file does not exists: " + path);
    return Result<Reports>(std::move(reports));
  }
  if (util::pypath::IsDir(path)) {
    kLog.Error("Directory is given instead of a file: " + path);
    return Result<Reports>(std::move(reports));
  }
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) return Result<Reports>(content.error());
  const std::string text =
      util::TranslateNewlines(util::DecodeUtf8Replace(content.value()));
  std::vector<std::string> lines;
  for (const std::string_view line : util::SplitLines(text)) {
    lines.emplace_back(line);
  }
  LineReader reader(std::move(lines));
  std::optional<std::string> line = reader.Next();
  while (line) {
    Result<std::optional<std::string>> next =
        parse_line(&reader, *line, &reports);
    if (!next.ok()) return Result<Reports>(next.error());
    line = next.value();
  }
  return Result<Reports>(std::move(reports));
}

int ToInt(std::string_view digits) {
  return static_cast<int>(util::ParseInt(digits).value_or(0));
}

std::string RelativeToInput(const std::string& input, std::string_view path) {
  return util::pypath::Join({util::pypath::Dirname(input), path});
}

}  // namespace cc::report
