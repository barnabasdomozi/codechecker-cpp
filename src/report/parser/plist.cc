#include "report/parser/plist.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/plist.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

constexpr util::Logger kLog("report-converter");

// Python: value[key] for a plist dictionary.
Result<yyjson_val*> Get(yyjson_val* value, std::string_view key) {
  return util::JsonSubscript(value, key);
}

// Python: int(value[key]).
Result<int> GetInt(yyjson_val* value, std::string_view key) {
  const Result<int64_t> number = util::JsonSubscriptInt(value, key);
  if (!number.ok()) return Result<int>(number.error());
  return Result<int>(static_cast<int>(number.value()));
}

// Reads the diagnostics of one plist; `files` are the files by index.
class DiagnosticReader {
 public:
  DiagnosticReader(const std::string* path, const std::vector<File>* files,
                   std::optional<std::string> metadata_analyzer)
      : path_(path),
        files_(files),
        metadata_analyzer_(std::move(metadata_analyzer)) {}

  DiagnosticReader(const DiagnosticReader&) = delete;
  DiagnosticReader& operator=(const DiagnosticReader&) = delete;

  // Python: __create_report().
  Result<Report> ReadReport(yyjson_val* diagnostic) const;

 private:
  // Python: files[location['file']].
  Result<File> FileOf(yyjson_val* location) const;
  // Python: __get_bug_event_locations() and the event built from them.
  Result<BugPathEvent> ReadEvent(yyjson_val* item,
                                 std::string_view message_key) const;
  Result<std::vector<BugPathPosition>> ReadPositions(yyjson_val* path) const;

  const std::string* path_;
  const std::vector<File>* files_;
  std::optional<std::string> metadata_analyzer_;
};

Result<File> DiagnosticReader::FileOf(yyjson_val* location) const {
  Result<yyjson_val*> index = Get(location, "file");
  if (!index.ok()) return Result<File>(index.error());
  const std::optional<int64_t> number = util::JsonToInt(index.value());
  if (!number || *number < 0 ||
      *number >= static_cast<int64_t>(files_->size())) {
    return Result<File>(
        Error{.message = "KeyError: " + util::JsonToStr(index.value())});
  }
  return Result<File>((*files_)[*number]);
}

Result<BugPathEvent> DiagnosticReader::ReadEvent(
    yyjson_val* item, std::string_view message_key) const {
  Result<yyjson_val*> location = Get(item, "location");
  if (!location.ok()) return Result<BugPathEvent>(location.error());
  yyjson_val* start = location.value();
  yyjson_val* end = location.value();
  yyjson_val* ranges = util::JsonGet(item, "ranges");
  if (yyjson_arr_size(ranges) > 0) {
    yyjson_val* first = yyjson_arr_get_first(ranges);
    start = yyjson_arr_get(first, 0);
    end = yyjson_arr_get(first, 1);
  }
  Result<yyjson_val*> message = Get(item, message_key);
  Result<File> file = FileOf(location.value());
  Result<int> line = GetInt(location.value(), "line");
  Result<int> column = GetInt(location.value(), "col");
  Result<int> start_line = GetInt(start, "line");
  Result<int> start_col = GetInt(start, "col");
  Result<int> end_line = GetInt(end, "line");
  Result<int> end_col = GetInt(end, "col");
  for (const Error* error :
       {message.ok() ? nullptr : &message.error(),
        file.ok() ? nullptr : &file.error(),
        line.ok() ? nullptr : &line.error(),
        column.ok() ? nullptr : &column.error(),
        start_line.ok() ? nullptr : &start_line.error(),
        start_col.ok() ? nullptr : &start_col.error(),
        end_line.ok() ? nullptr : &end_line.error(),
        end_col.ok() ? nullptr : &end_col.error()}) {
    if (error != nullptr) return Result<BugPathEvent>(*error);
  }
  return Result<BugPathEvent>(BugPathEvent{
      .message = util::JsonToStr(message.value()),
      .file = file.value(),
      .line = line.value(),
      .column = column.value(),
      .range = Range{start_line.value(), start_col.value(), end_line.value(),
                     end_col.value()}});
}

Result<std::vector<BugPathPosition>> DiagnosticReader::ReadPositions(
    yyjson_val* path) const {
  using Positions = std::vector<BugPathPosition>;
  Positions positions;
  // The path A->B->C is given as A->B and B->C, so a start equal to the
  // previous end is not stored again.
  yyjson_val* previous_end = nullptr;
  const auto range_of = [this](yyjson_val* edge) -> Result<BugPathPosition> {
    yyjson_val* first = yyjson_arr_get(edge, 0);
    yyjson_val* second = yyjson_arr_get(edge, 1);
    Result<File> file = FileOf(second);
    if (!file.ok()) return Result<BugPathPosition>(file.error());
    Result<int> start_line = GetInt(first, "line");
    Result<int> start_col = GetInt(first, "col");
    Result<int> end_line = GetInt(second, "line");
    Result<int> end_col = GetInt(second, "col");
    if (!start_line.ok() || !start_col.ok() || !end_line.ok() ||
        !end_col.ok()) {
      return Result<BugPathPosition>(Error{.message = "KeyError: 'line'"});
    }
    return Result<BugPathPosition>(BugPathPosition{
        .file = file.value(),
        .range = Range{start_line.value(), start_col.value(),
                       end_line.value(), end_col.value()}});
  };
  const auto same_range = [](yyjson_val* a, yyjson_val* b) {
    for (size_t i = 0; i < 2; ++i) {
      if (!yyjson_equals(yyjson_arr_get(a, i), yyjson_arr_get(b, i))) {
        return false;
      }
    }
    return true;
  };
  yyjson_arr_iter iter = yyjson_arr_iter_with(path);
  while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
    if (util::JsonToStr(util::JsonGet(item, "kind")) != "control") continue;
    yyjson_val* edges = yyjson_arr_get_first(util::JsonGet(item, "edges"));
    if (edges == nullptr) continue;  // No edges: nothing to store.
    yyjson_val* start = util::JsonGet(edges, "start");
    yyjson_val* end = util::JsonGet(edges, "end");
    if (start == nullptr || end == nullptr) {
      return Result<Positions>(
          Error{.message = start == nullptr ? "KeyError: 'start'"
                                            : "KeyError: 'end'"});
    }
    if (previous_end == nullptr || !same_range(start, previous_end)) {
      Result<BugPathPosition> position = range_of(start);
      if (!position.ok()) return Result<Positions>(position.error());
      positions.push_back(position.value());
    }
    Result<BugPathPosition> position = range_of(end);
    if (!position.ok()) return Result<Positions>(position.error());
    positions.push_back(position.value());
    previous_end = end;
  }
  return Result<Positions>(std::move(positions));
}

Result<Report> DiagnosticReader::ReadReport(yyjson_val* diagnostic) const {
  yyjson_val* location = util::JsonGet(diagnostic, "location");
  Result<File> file = FileOf(location);
  if (!file.ok()) return Result<Report>(file.error());
  yyjson_val* line = util::JsonGet(location, "line");
  yyjson_val* column = util::JsonGet(location, "col");
  yyjson_val* checker = util::JsonGet(diagnostic, "check_name");
  yyjson_val* message = util::JsonGet(diagnostic, "description");
  const std::string checker_name =
      checker != nullptr ? util::JsonToStr(checker) : "NOT FOUND";

  std::vector<BugPathEvent> events;
  yyjson_val* path = util::JsonGet(diagnostic, "path");
  yyjson_arr_iter iter = yyjson_arr_iter_with(path);
  while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
    if (util::JsonToStr(util::JsonGet(item, "kind")) != "event") continue;
    Result<BugPathEvent> event = ReadEvent(item, "message");
    if (!event.ok()) return Result<Report>(event.error());
    events.push_back(event.value());
  }

  Report report = MakeReport(
      file.value(),
      line != nullptr ? static_cast<int>(util::JsonToInt(line).value_or(-1))
                      : -1,
      column != nullptr
          ? static_cast<int>(util::JsonToInt(column).value_or(-1))
          : -1,
      message != nullptr ? util::JsonToStr(message) : "", checker_name,
      std::move(events));
  report.analyzer_result_file_path = *path_;
  if (metadata_analyzer_) {
    report.analyzer_name = metadata_analyzer_;
  } else if (checker_name.starts_with("clang-diagnostic-")) {
    report.analyzer_name = "clang-tidy";
  }
  if (yyjson_val* hash =
          util::JsonGet(diagnostic, "issue_hash_content_of_line_in_context");
      hash != nullptr && !yyjson_is_null(hash)) {
    report.report_hash = util::JsonToStr(hash);
  }
  if (yyjson_val* category = util::JsonGet(diagnostic, "category");
      category != nullptr && !yyjson_is_null(category)) {
    report.category = util::JsonToStr(category);
  }

  Result<std::vector<BugPathPosition>> positions = ReadPositions(path);
  if (!positions.ok()) return Result<Report>(positions.error());
  report.bug_path_positions = std::move(positions).value();

  yyjson_val* notes = util::JsonGet(diagnostic, "notes");
  iter = yyjson_arr_iter_with(notes);
  while (yyjson_val* note = yyjson_arr_iter_next(&iter)) {
    Result<yyjson_val*> text = Get(note, "message");
    if (!text.ok()) return Result<Report>(text.error());
    if (util::JsonToStr(text.value()).empty()) continue;
    Result<BugPathEvent> event = ReadEvent(note, "message");
    if (!event.ok()) return Result<Report>(event.error());
    report.notes.push_back(event.value());
  }

  yyjson_val* macros = util::JsonGet(diagnostic, "macro_expansions");
  iter = yyjson_arr_iter_with(macros);
  while (yyjson_val* macro = yyjson_arr_iter_next(&iter)) {
    Result<yyjson_val*> expansion = Get(macro, "expansion");
    if (!expansion.ok()) return Result<Report>(expansion.error());
    if (util::JsonToStr(expansion.value()).empty()) continue;
    Result<BugPathEvent> event = ReadEvent(macro, "expansion");
    Result<yyjson_val*> name = Get(macro, "name");
    if (!event.ok()) return Result<Report>(event.error());
    if (!name.ok()) return Result<Report>(name.error());
    report.macro_expansions.push_back(
        {.event = event.value(), .name = util::JsonToStr(name.value())});
  }

  yyjson_val* annotations = util::JsonGet(diagnostic, "report-annotation");
  yyjson_obj_iter annotation_iter = yyjson_obj_iter_with(annotations);
  while (yyjson_val* key = yyjson_obj_iter_next(&annotation_iter)) {
    report.annotations.emplace_back(
        util::JsonToStr(key),
        util::JsonToStr(yyjson_obj_iter_get_val(key)));
  }
  return Result<Report>(std::move(report));
}

// Returns the files of a plist as Python prints its file index map, for
// example {0: {"id": "/a.c", "path": "/a.c", "original_path": "/a.c"}}.
std::string FilesRepr(const std::vector<File>& files) {
  std::vector<std::string> items;
  for (size_t i = 0; i < files.size(); ++i) {
    const util::MutableJsonDocument document = util::NewJsonDocument();
    yyjson_mut_doc* doc = document.get();
    yyjson_mut_val* object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, object, "id", files[i].id.c_str());
    yyjson_mut_obj_add_strcpy(doc, object, "path", files[i].path.c_str());
    yyjson_mut_obj_add_strcpy(doc, object, "original_path",
                              files[i].original_path.c_str());
    items.push_back(std::format("{}: {}", i, util::DumpJson(object)));
  }
  return "{" + util::Join(items, ", ") + "}";
}

}  // namespace

std::vector<Report> ReadPlistFile(const std::string& path) {
  std::vector<Report> reports;
  const Result<std::string> content = util::ReadFile(path);
  if (!content.ok()) {
    kLog.Warning("Error during processing reports from the plist file: " +
                 path);
    kLog.Warning(content.error().message);
    return reports;
  }
  const Result<util::JsonDocument> document =
      util::ParsePlist(content.value(), path);
  if (!document.ok()) {
    kLog.Error(std::format("Invalid plist file '{}': {}", path,
                           document.error().message));
    return reports;
  }
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  if (yyjson_is_null(root) ||
      (yyjson_is_obj(root) && yyjson_obj_size(root) == 0)) {
    return reports;
  }
  if (!yyjson_is_obj(root)) {
    kLog.Warning("Error during processing reports from the plist file: " +
                 path);
    return reports;
  }

  const std::string source_dir = util::pypath::Dirname(path);
  std::vector<File> files;
  yyjson_arr_iter iter = yyjson_arr_iter_with(util::JsonGet(root, "files"));
  while (yyjson_val* file = yyjson_arr_iter_next(&iter)) {
    files.push_back(MakeFile(util::pypath::Normpath(
        util::pypath::Join({source_dir, util::JsonToStr(file)}))));
  }
  yyjson_val* metadata_analyzer =
      util::JsonGet(util::JsonGet(util::JsonGet(root, "metadata"), "analyzer"),
                    "name");
  std::optional<std::string> analyzer_name;
  if (metadata_analyzer != nullptr && yyjson_is_str(metadata_analyzer) &&
      yyjson_get_len(metadata_analyzer) > 0) {
    analyzer_name = util::JsonToStr(metadata_analyzer);
  }

  const DiagnosticReader reader(&path, &files, analyzer_name);
  iter = yyjson_arr_iter_with(util::JsonGet(root, "diagnostics"));
  while (yyjson_val* diagnostic = yyjson_arr_iter_next(&iter)) {
    Result<Report> report = reader.ReadReport(diagnostic);
    if (!report.ok()) {
      kLog.Warning(std::format(
          "Failed to get file path id! Found files: {}. {}",
          FilesRepr(files), report.error().message));
      return reports;
    }
    Report value = std::move(report).value();
    if (!value.report_hash) {
      value.report_hash = GetReportHash(value, HashType::kPathSensitive);
    }
    reports.push_back(std::move(value));
  }
  return reports;
}

}  // namespace cc::report
