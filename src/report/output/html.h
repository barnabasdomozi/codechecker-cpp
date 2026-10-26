// Port of codechecker_report_converter/report/output/html/html.py: the HTML
// export of `parse`, pages that show the reports on their source code and
// need no server (parse-export.md "HTML").

#ifndef CC_REPORT_OUTPUT_HTML_H_
#define CC_REPORT_OUTPUT_HTML_H_

#include <yyjson.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "report/model/report.h"
#include "report/output/checker_info.h"
#include "report/output/statistics.h"
#include "util/result.h"

namespace cc::report {

// Writes the HTML pages of the reports into an output folder: one page per
// result file with Convert(), then the report list and the statistics with
// Finish(). Not thread-safe.
//
//   Result<HtmlBuilder> builder = HtmlBuilder::Create(layout_dir, lookup);
//   for (...) builder.value().Convert(result_file, reports, output_dir);
//   builder.value().Finish(output_dir, statistics);
class HtmlBuilder {
 public:
  // Returns a builder that fills the templates of `layout_dir` (the static
  // folder of the Python package), with the checker severities and
  // documentation links of `checker_info` (empty function: none). Fails
  // with the text of Python's OSError if a file cannot be read.
  static Result<HtmlBuilder> Create(const std::string& layout_dir,
                                    CheckerInfoLookup checker_info);

  HtmlBuilder(HtmlBuilder&& other) = default;
  HtmlBuilder& operator=(HtmlBuilder&& other) = default;

  // Writes the page of `reports`, the reports of the result file
  // `result_file`, into `output_dir` as <result file name>.html, unless a
  // file of the reports changed since the analysis (Python: convert()).
  // Fails with the text of Python's exception.
  Result<void> Convert(const std::string& result_file,
                       const std::vector<Report>& reports,
                       const std::string& output_dir);

  // Writes index.html and statistics.html of the pages into `output_dir`,
  // then prints `statistics` and how to open the pages (Python: finish()).
  Result<void> Finish(const std::string& output_dir,
                      const Statistics& statistics);

 private:
  // A source file, as the pages embed it (Python: FileSource).
  struct FileSource {
    std::string path;     // Trimmed, as it was when first used.
    std::string content;  // HTML-escaped.
  };

  // What index.html and statistics.html show of a report.
  struct ListedReport {
    std::string path;
    std::optional<std::string> report_hash;
    std::string checker_name;
    std::string checker_url;
    int line = 0;
    std::string message;
    std::string review_status;
    std::string severity;
    int bug_path_length = 0;
    std::optional<std::string> testcase;
    std::optional<std::string> timestamp;
    std::optional<std::string> chronological_order;
  };

  // A written page and its reports.
  struct Page {
    std::string path;
    std::vector<ListedReport> reports;
  };

  explicit HtmlBuilder(CheckerInfoLookup checker_info)
      : checker_info_(std::move(checker_info)) {}

  // Returns the severity of `checker` (Python: get_severity()).
  std::string Severity(const std::string& checker) const;

  // Returns the embedded source of `file`, read on the first use (Python:
  // _add_source_file()).
  const FileSource& AddSourceFile(const File& file);

  // Returns what index.html and statistics.html show of `report`.
  ListedReport ListReport(const Report& report) const;

  // Adds `file` to `files`, the files object of the page built in `doc`,
  // unless it is there already.
  void AddFileJson(yyjson_mut_doc* doc, yyjson_mut_val* files,
                   const File& file);

  // Returns `events` as the page shows them, and adds their files to
  // `files`.
  yyjson_mut_val* EventsJson(yyjson_mut_doc* doc, yyjson_mut_val* files,
                             const std::vector<BugPathEvent>& events);

  // Returns `report`, listed as `listed`, as the page shows it, and adds
  // its files to `files`.
  yyjson_mut_val* ReportJson(yyjson_mut_doc* doc, yyjson_mut_val* files,
                             const Report& report, const ListedReport& listed);

  // Writes `page_template`, its placeholders filled from values_, into the
  // file at `path`.
  Result<void> WritePage(const std::string& page_template,
                         const std::string& path) const;

  // Writes index.html, the list of the reports of the pages, into the file
  // at `path` (Python: create_index_html()).
  Result<void> WriteIndex(const std::string& path);

  // Writes statistics.html, the report counts of the pages by checker and
  // severity, into the file at `path` (Python: create_statistics_html()).
  Result<void> WriteStatistics(const std::string& path);

  CheckerInfoLookup checker_info_;
  std::string layout_template_;      // The page of a result file.
  std::string index_template_;       // The list of the reports.
  std::string statistics_template_;  // The statistics.
  // The values of the template placeholders: the static files, and what
  // the page being written shows (Python: _tag_contents).
  std::map<std::string, std::string, std::less<>> values_;
  std::unordered_map<std::string, FileSource> files_;  // By file id.
  std::vector<Page> pages_;  // In the order they were first written.
};

}  // namespace cc::report

#endif  // CC_REPORT_OUTPUT_HTML_H_
