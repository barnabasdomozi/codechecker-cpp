#include "report/output/sarif.h"

#include <yyjson.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "report/output/tool_info.h"
#include "util/file.h"
#include "util/json.h"
#include "util/result.h"

namespace cc::report {
namespace {

yyjson_mut_val* String(yyjson_mut_doc* doc, std::string_view text) {
  return yyjson_mut_strncpy(doc, text.data(), text.size());
}

// Returns {"text": text}.
yyjson_mut_val* Text(yyjson_mut_doc* doc, std::string_view text) {
  yyjson_mut_val* object = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, object, "text", String(doc, text));
  return object;
}

// Returns a location with a physical location in `file` (Python:
// _create_location()): the full range if there is one, else the line and
// column.
yyjson_mut_val* Location(yyjson_mut_doc* doc, const File& file,
                         const std::optional<Range>& range, int line,
                         int column) {
  yyjson_mut_val* region = yyjson_mut_obj(doc);
  if (range) {
    yyjson_mut_obj_add_int(doc, region, "startLine", range->start_line);
    yyjson_mut_obj_add_int(doc, region, "startColumn", range->start_col);
    yyjson_mut_obj_add_int(doc, region, "endLine", range->end_line);
    yyjson_mut_obj_add_int(doc, region, "endColumn", range->end_col);
  } else {
    yyjson_mut_obj_add_int(doc, region, "startLine", line);
    yyjson_mut_obj_add_int(doc, region, "startColumn", column);
  }
  yyjson_mut_val* artifact = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, artifact, "uri",
                         String(doc, "file://" + file.path));
  yyjson_mut_val* physical = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, physical, "artifactLocation", artifact);
  yyjson_mut_obj_add_val(doc, physical, "region", region);
  yyjson_mut_val* location = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, location, "physicalLocation", physical);
  return location;
}

// Returns a thread flow location for an event, note or macro expansion.
yyjson_mut_val* EventLocation(yyjson_mut_doc* doc, const BugPathEvent& event,
                              const char* importance) {
  yyjson_mut_val* location =
      Location(doc, event.file, event.range, event.line, event.column);
  yyjson_mut_obj_add_val(doc, location, "message", Text(doc, event.message));
  yyjson_mut_val* item = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, item, "location", location);
  yyjson_mut_obj_add_str(doc, item, "importance", importance);
  return item;
}

yyjson_mut_val* ResultObject(yyjson_mut_doc* doc, const Report& report) {
  yyjson_mut_val* result = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, result, "ruleId",
                         String(doc, report.checker_name));
  yyjson_mut_obj_add_val(doc, result, "message", Text(doc, report.message));
  yyjson_mut_val* locations = yyjson_mut_arr(doc);
  yyjson_mut_arr_append(locations, Location(doc, report.file, std::nullopt,
                                            report.line, report.column));
  yyjson_mut_obj_add_val(doc, result, "locations", locations);

  yyjson_mut_val* flow = yyjson_mut_arr(doc);
  for (const BugPathEvent& event : report.bug_path_events) {
    yyjson_mut_arr_append(flow, EventLocation(doc, event, "important"));
  }
  for (const BugPathEvent& note : report.notes) {
    yyjson_mut_arr_append(flow, EventLocation(doc, note, "essential"));
  }
  for (const MacroExpansion& macro : report.macro_expansions) {
    yyjson_mut_arr_append(flow, EventLocation(doc, macro.event, "essential"));
  }
  for (const BugPathPosition& position : report.bug_path_positions) {
    yyjson_mut_val* item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(
        doc, item, "location",
        Location(doc, position.file, position.range, -1, -1));
    yyjson_mut_arr_append(flow, item);
  }
  if (yyjson_mut_arr_size(flow) > 0) {
    yyjson_mut_val* thread_flow = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, thread_flow, "locations", flow);
    yyjson_mut_val* thread_flows = yyjson_mut_arr(doc);
    yyjson_mut_arr_append(thread_flows, thread_flow);
    yyjson_mut_val* code_flow = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, code_flow, "threadFlows", thread_flows);
    yyjson_mut_val* code_flows = yyjson_mut_arr(doc);
    yyjson_mut_arr_append(code_flows, code_flow);
    yyjson_mut_obj_add_val(doc, result, "codeFlows", code_flows);
  }
  return result;
}

}  // namespace

Result<void> WriteSarifFile(const std::string& path,
                            const std::vector<Report>& reports) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  const ToolInfo tool = GetToolInfo();

  std::set<std::string> seen_checkers;
  yyjson_mut_val* rules = yyjson_mut_arr(doc);
  yyjson_mut_val* results = yyjson_mut_arr(doc);
  for (const Report& report : reports) {
    if (seen_checkers.insert(report.checker_name).second) {
      yyjson_mut_val* rule = yyjson_mut_obj(doc);
      yyjson_mut_obj_add_val(doc, rule, "id",
                             String(doc, report.checker_name));
      yyjson_mut_obj_add_val(doc, rule, "fullDescription",
                             Text(doc, report.message));
      yyjson_mut_arr_append(rules, rule);
    }
    yyjson_mut_arr_append(results, ResultObject(doc, report));
  }

  yyjson_mut_val* driver = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, driver, "name", String(doc, tool.name));
  yyjson_mut_obj_add_val(doc, driver, "version", String(doc, tool.version));
  yyjson_mut_obj_add_val(doc, driver, "rules", rules);
  yyjson_mut_val* tool_value = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, tool_value, "driver", driver);
  yyjson_mut_val* run = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, run, "tool", tool_value);
  yyjson_mut_obj_add_val(doc, run, "results", results);
  yyjson_mut_val* runs = yyjson_mut_arr(doc);
  yyjson_mut_arr_append(runs, run);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, root, "version", "2.1.0");
  yyjson_mut_obj_add_str(doc, root, "$schema",
                         "https://raw.githubusercontent.com/oasis-tcs/"
                         "sarif-spec/master/Schemata/sarif-schema-2.1.0.json");
  yyjson_mut_obj_add_val(doc, root, "runs", runs);
  return util::WriteFile(path, util::DumpJson(root));
}

}  // namespace cc::report
