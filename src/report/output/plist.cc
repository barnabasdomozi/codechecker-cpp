#include "report/output/plist.h"

#include <stddef.h>
#include <yyjson.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "report/model/report.h"
#include "report/output/tool_info.h"
#include "util/file.h"
#include "util/json.h"
#include "util/plist.h"
#include "util/result.h"

namespace cc::report {
namespace {

// Builds the plist values of reports, with the file indexes of one result
// file.
class PlistConverter {
 public:
  PlistConverter(yyjson_mut_doc* document,
                 const std::map<std::string, int>* file_indexes)
      : doc_(document), file_indexes_(file_indexes) {}

  PlistConverter(const PlistConverter&) = delete;
  PlistConverter& operator=(const PlistConverter&) = delete;

  yyjson_mut_val* Diagnostic(const Report& report) const;

 private:
  yyjson_mut_val* String(std::string_view text) const {
    return yyjson_mut_strncpy(doc_, text.data(), text.size());
  }
  int Index(const File& file) const {
    return file_indexes_->at(file.original_path);
  }
  yyjson_mut_val* Location(int line, int column, int file_index) const;
  yyjson_mut_val* RangeValue(const Range& range, int file_index) const;
  yyjson_mut_val* ControlItem(const Range& start, const File& start_file,
                              const Range& end, const File& end_file) const;
  yyjson_mut_val* Event(const BugPathEvent& event) const;
  yyjson_mut_val* Note(const BugPathEvent& note) const;
  yyjson_mut_val* Macro(const MacroExpansion& macro) const;

  yyjson_mut_doc* doc_;
  const std::map<std::string, int>* file_indexes_;
};

yyjson_mut_val* PlistConverter::Location(int line, int column,
                                         int file_index) const {
  yyjson_mut_val* location = yyjson_mut_obj(doc_);
  yyjson_mut_obj_add_int(doc_, location, "line", line);
  yyjson_mut_obj_add_int(doc_, location, "col", column);
  yyjson_mut_obj_add_int(doc_, location, "file", file_index);
  return location;
}

yyjson_mut_val* PlistConverter::RangeValue(const Range& range,
                                           int file_index) const {
  yyjson_mut_val* value = yyjson_mut_arr(doc_);
  yyjson_mut_arr_append(
      value, Location(range.start_line, range.start_col, file_index));
  yyjson_mut_arr_append(value,
                        Location(range.end_line, range.end_col, file_index));
  return value;
}

yyjson_mut_val* PlistConverter::ControlItem(const Range& start,
                                            const File& start_file,
                                            const Range& end,
                                            const File& end_file) const {
  yyjson_mut_val* edge = yyjson_mut_obj(doc_);
  yyjson_mut_obj_add_val(doc_, edge, "start",
                         RangeValue(start, Index(start_file)));
  yyjson_mut_obj_add_val(doc_, edge, "end", RangeValue(end, Index(end_file)));
  yyjson_mut_val* edges = yyjson_mut_arr(doc_);
  yyjson_mut_arr_append(edges, edge);
  yyjson_mut_val* item = yyjson_mut_obj(doc_);
  yyjson_mut_obj_add_str(doc_, item, "kind", "control");
  yyjson_mut_obj_add_val(doc_, item, "edges", edges);
  return item;
}

yyjson_mut_val* PlistConverter::Event(const BugPathEvent& event) const {
  const int index = Index(event.file);
  yyjson_mut_val* item = yyjson_mut_obj(doc_);
  yyjson_mut_obj_add_str(doc_, item, "kind", "event");
  yyjson_mut_obj_add_val(doc_, item, "location",
                         Location(event.line, event.column, index));
  yyjson_mut_obj_add_int(doc_, item, "depth", 0);
  yyjson_mut_obj_add_val(doc_, item, "message", String(event.message));
  if (event.range) {
    yyjson_mut_val* ranges = yyjson_mut_arr(doc_);
    yyjson_mut_arr_append(ranges, RangeValue(*event.range, index));
    yyjson_mut_obj_add_val(doc_, item, "ranges", ranges);
  }
  return item;
}

yyjson_mut_val* PlistConverter::Note(const BugPathEvent& note) const {
  const int index = Index(note.file);
  yyjson_mut_val* item = yyjson_mut_obj(doc_);
  yyjson_mut_obj_add_val(doc_, item, "location",
                         Location(note.line, note.column, index));
  yyjson_mut_obj_add_val(doc_, item, "message", String(note.message));
  if (note.range) {
    yyjson_mut_val* ranges = yyjson_mut_arr(doc_);
    yyjson_mut_arr_append(ranges, RangeValue(*note.range, index));
    yyjson_mut_obj_add_val(doc_, item, "ranges", ranges);
  }
  return item;
}

yyjson_mut_val* PlistConverter::Macro(const MacroExpansion& macro) const {
  yyjson_mut_val* item = yyjson_mut_obj(doc_);
  yyjson_mut_obj_add_val(doc_, item, "name", String(macro.name));
  yyjson_mut_obj_add_val(doc_, item, "expansion", String(macro.event.message));
  yyjson_mut_obj_add_val(
      doc_, item, "location",
      Location(macro.event.line, macro.event.column, Index(macro.event.file)));
  return item;
}

yyjson_mut_val* PlistConverter::Diagnostic(const Report& report) const {
  yyjson_mut_val* diagnostic = yyjson_mut_obj(doc_);
  const auto add = [this, diagnostic](const char* key, yyjson_mut_val* value) {
    yyjson_mut_obj_add_val(doc_, diagnostic, key, value);
  };
  add("location", Location(report.line, report.column, Index(report.file)));
  add("issue_hash_content_of_line_in_context",
      report.report_hash ? String(*report.report_hash) : yyjson_mut_null(doc_));
  add("check_name", String(report.checker_name));
  add("description", String(report.message));
  add("category", String(report.category && !report.category->empty()
                             ? *report.category
                             : "unknown"));
  if (report.analyzer_name && !report.analyzer_name->empty()) {
    add("type", String(*report.analyzer_name));
  }

  yyjson_mut_val* path = yyjson_mut_arr(doc_);
  const std::vector<BugPathPosition>& positions = report.bug_path_positions;
  const std::vector<BugPathEvent>& events = report.bug_path_events;
  if (!positions.empty()) {
    for (size_t i = 0; i + 1 < positions.size(); ++i) {
      if (positions[i].range && positions[i + 1].range) {
        yyjson_mut_arr_append(
            path, ControlItem(*positions[i].range, positions[i].file,
                              *positions[i + 1].range, positions[i + 1].file));
      }
    }
  } else if (events.size() > 1) {
    // Control items made from the events.
    const auto range_of = [](const BugPathEvent& event) {
      return event.range.value_or(
          Range{event.line, event.column, event.line, event.column});
    };
    for (size_t i = 0; i + 1 < events.size(); ++i) {
      const Range start = range_of(events[i]);
      const Range end = range_of(events[i + 1]);
      if (start == end) continue;
      yyjson_mut_arr_append(
          path, ControlItem(start, events[i].file, end, events[i + 1].file));
    }
  }
  for (const BugPathEvent& event : events) {
    yyjson_mut_arr_append(path, Event(event));
  }
  add("path", path);

  if (!report.notes.empty()) {
    yyjson_mut_val* notes = yyjson_mut_arr(doc_);
    for (const BugPathEvent& note : report.notes) {
      yyjson_mut_arr_append(notes, Note(note));
    }
    add("notes", notes);
  }
  if (!report.macro_expansions.empty()) {
    yyjson_mut_val* macros = yyjson_mut_arr(doc_);
    for (const MacroExpansion& macro : report.macro_expansions) {
      yyjson_mut_arr_append(macros, Macro(macro));
    }
    add("macro_expansions", macros);
  }
  if (!report.annotations.empty()) {
    yyjson_mut_val* annotations = yyjson_mut_obj(doc_);
    for (const auto& [key, value] : report.annotations) {
      yyjson_mut_obj_put(annotations, String(key), String(value));
    }
    add("report-annotation", annotations);
  }
  return diagnostic;
}

}  // namespace

Result<void> WritePlistFile(const std::string& path,
                            const std::vector<Report>& reports,
                            const std::optional<std::string>& analyzer_name) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  const ToolInfo tool = GetToolInfo();

  std::set<std::string> files;
  for (const Report& report : reports) files.merge(OriginalFiles(report));
  std::map<std::string, int> file_indexes;
  yyjson_mut_val* file_list = yyjson_mut_arr(doc);
  for (const std::string& file : files) {
    file_indexes.emplace(file, static_cast<int>(file_indexes.size()));
    yyjson_mut_arr_append(file_list,
                          yyjson_mut_strncpy(doc, file.data(), file.size()));
  }

  const PlistConverter converter(doc, &file_indexes);
  yyjson_mut_val* diagnostics = yyjson_mut_arr(doc);
  for (const Report& report : reports) {
    yyjson_mut_arr_append(diagnostics, converter.Diagnostic(report));
  }

  yyjson_mut_val* generated_by = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_strncpy(doc, generated_by, "name", tool.name.data(),
                             tool.name.size());
  yyjson_mut_obj_add_strncpy(doc, generated_by, "version",
                             tool.version.data(), tool.version.size());
  yyjson_mut_val* metadata = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, metadata, "generated_by", generated_by);
  if (analyzer_name) {
    yyjson_mut_val* analyzer = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strncpy(doc, analyzer, "name", analyzer_name->data(),
                               analyzer_name->size());
    yyjson_mut_obj_add_val(doc, metadata, "analyzer", analyzer);
  }

  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, root, "files", file_list);
  yyjson_mut_obj_add_val(doc, root, "diagnostics", diagnostics);
  yyjson_mut_obj_add_val(doc, root, "metadata", metadata);
  const Result<std::string> text = util::DumpPlist(root);
  if (!text.ok()) {
    return Result<void>(
        Error{.message = "ValueError: " + text.error().message});
  }
  return util::WriteFile(path, text.value());
}

}  // namespace cc::report
