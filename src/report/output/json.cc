#include "report/output/json.h"

#include <yyjson.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/result.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

}  // namespace

std::string ReportsToJson(const std::vector<Report>& reports) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* json_reports = yyjson_mut_arr(doc);
  for (const Report& report : reports) {
    yyjson_mut_arr_append(json_reports, ReportToJson(report, doc));
  }
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, root, "version", 1);
  yyjson_mut_obj_add_val(doc, root, "reports", json_reports);
  return util::DumpJson(root);
}

Result<void> DumpJsonOutput(std::string_view json,
                            const std::optional<std::string>& output_path) {
  if (!output_path) {
    util::Write(util::Stream::kStdout, std::string(json) + "\n");
    return Result<void>();
  }
  if (Result<void> written = util::WriteFile(*output_path, json);
      !written.ok()) {
    return written;
  }
  kLog.Info("JSON report file was created: " + *output_path);
  return Result<void>();
}

}  // namespace cc::report
