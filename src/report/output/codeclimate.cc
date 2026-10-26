#include "report/output/codeclimate.h"

#include <yyjson.h>

#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "util/json.h"

namespace cc::report {
namespace {

// A severity label and the Code Climate severity it maps to.
struct SeverityMapping {
  std::string_view severity;
  const char* code_climate;
};

constexpr SeverityMapping kSeverityMap[] = {
    {.severity = "CRITICAL", .code_climate = "critical"},
    {.severity = "HIGH", .code_climate = "major"},
    {.severity = "MEDIUM", .code_climate = "minor"},
    {.severity = "LOW", .code_climate = "minor"},
    {.severity = "STYLE", .code_climate = "info"},
    {.severity = "UNSPECIFIED", .code_climate = "info"}};

// Returns the Code Climate severity of `report`: "info" without a severity,
// null for a severity the map does not know.
yyjson_mut_val* Severity(yyjson_mut_doc* doc, const Report& report) {
  if (!report.severity || report.severity->empty()) {
    return yyjson_mut_str(doc, "info");
  }
  for (const SeverityMapping& mapping : kSeverityMap) {
    if (mapping.severity == *report.severity) {
      return yyjson_mut_str(doc, mapping.code_climate);
    }
  }
  return yyjson_mut_null(doc);
}

// Returns the Code Climate issue of `report` (Python: __to_codeclimate()).
yyjson_mut_val* Issue(yyjson_mut_doc* doc, const Report& report) {
  yyjson_mut_val* issue = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, issue, "type", "issue");
  yyjson_mut_obj_add_val(doc, issue, "check_name",
                         util::MakeJsonString(doc, report.checker_name));
  yyjson_mut_obj_add_val(doc, issue, "description",
                         util::MakeJsonString(doc, report.message));
  yyjson_mut_val* categories = yyjson_mut_arr(doc);
  yyjson_mut_arr_add_str(doc, categories, "Bug Risk");
  yyjson_mut_obj_add_val(doc, issue, "categories", categories);
  yyjson_mut_obj_add_val(
      doc, issue, "fingerprint",
      util::MakeJsonOptionalString(doc, report.report_hash));
  yyjson_mut_obj_add_val(doc, issue, "severity", Severity(doc, report));
  yyjson_mut_val* lines = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, lines, "begin", report.line);
  yyjson_mut_val* location = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, location, "path",
                         util::MakeJsonString(doc, report.file.path));
  yyjson_mut_obj_add_val(doc, location, "lines", lines);
  yyjson_mut_obj_add_val(doc, issue, "location", location);
  return issue;
}

}  // namespace

std::string ReportsToCodeClimate(const std::vector<Report>& reports) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* issues = yyjson_mut_arr(doc);
  for (const Report& report : reports) {
    yyjson_mut_arr_append(issues, Issue(doc, report));
  }
  return util::DumpJson(issues);
}

}  // namespace cc::report
