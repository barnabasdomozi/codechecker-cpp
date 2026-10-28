#include "client/store/assemble_zip.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/codeCheckerProductService.h"
#include "api/thrift/products_types.h"
#include "client/client.h"
#include "client/store/blame_info.h"
#include "common/checker_labels.h"
#include "common/source_code_comment_handler.h"
#include "common/util.h"
#include "platform/filesystem.h"
#include "platform/subprocess.h"
#include "report/hash/report_hash.h"
#include "report/model/report.h"
#include "report/output/plist.h"
#include "report/output/statistics.h"
#include "report/parser/report_file.h"
#include "util/compression.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/parallel.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"
#include "util/uuid.h"
#include "util/zip.h"

namespace cc::client::store {
namespace {

constexpr util::Logger kLog("system");

// The error when the product takes fewer reports than the zip has; {} is
// the report limit of the product.
constexpr std::string_view kReportLimitExceeded = R"(Report Limit Exceeded

This report folder cannot be stored because the number of reports in the
result folder is too high. Usually noisy checkers, generating a lot of
reports are not useful and it is better to disable them.

Run `CodeChecker parse <report_folder>` to gain a better understanding on
report counts.

Disable checkers that have generated an excessive number of reports and then
rerun the analysis to be able to store the results on the server.

Configured report limit for this product: {}
        )";

// The files of the report folders to zip, by report folder. A conf/ folder
// stands for the files in it.
using FolderFiles = std::map<std::string, std::set<std::string>>;

// The reports without duplicates, by result folder and analyzer name ("" if
// it has none).
using UniqueReports =
    std::map<std::string, std::map<std::string, std::vector<report::Report>>>;

// The counts of the summary (Python: StorageZipStatistics).
struct ZipStatistics {
  report::Statistics reports;
  int source_files = 0;
  int source_files_with_comments = 0;
  int blame_files = 0;
};

// What the reports of the result files refer to.
struct ReportedFiles {
  UniqueReports unique_reports;
  int unique_report_count = 0;
  // The original paths of every file the reports refer to.
  std::set<std::string> source_files;
  // The lines of the reports in each main source file.
  std::map<std::string, std::set<int>> report_lines;
  // The files that changed since the analysis.
  std::set<std::string> changed_files;
};

// Returns the result files of `inputs`, each once, and adds the other files
// of their report folders to `folder_files`: metadata.json, skip_file
// (logged), review_status.yaml and conf/.
std::vector<std::string> CollectResultFiles(
    const std::vector<std::string>& inputs, FolderFiles& folder_files) {
  std::vector<std::string> result_files;
  std::set<std::string> seen;
  for (const report::ResultFolder& folder :
       report::AnalyzerResultFiles(inputs)) {
    for (const std::string& file : folder.files) {
      if (seen.insert(file).second) result_files.push_back(file);
    }
    const std::string metadata =
        util::pypath::Join({folder.path, "metadata.json"});
    if (util::pypath::Exists(metadata)) {
      folder_files[folder.path].insert(metadata);
    }
    const std::string skip_file =
        util::pypath::Join({folder.path, "skip_file"});
    if (util::pypath::Exists(skip_file)) {
      const Result<std::string> content = util::ReadTextFile(skip_file);
      kLog.Info(std::format(
          "Found skip file {} with the following content:\n{}", skip_file,
          content.ok() ? content.value() : std::string()));
      folder_files[folder.path].insert(skip_file);
    }
    const std::string review_status =
        util::pypath::Join({folder.path, "review_status.yaml"});
    if (util::pypath::Exists(review_status)) {
      folder_files[folder.path].insert(review_status);
    }
    const std::string conf = util::pypath::Join({folder.path, "conf"});
    if (util::pypath::IsDir(conf)) folder_files[folder.path].insert(conf);
  }
  return result_files;
}

// Returns the reports of each of `files`, with their severities, read on
// `jobs` threads (Python: parse_analyzer_result_files()). Fails if all the
// reports of a file have the report hash "0", which is logged.
Result<std::vector<std::vector<report::Report>>> ParseResultFiles(
    const std::vector<std::string>& files, const common::CheckerLabels& labels,
    int jobs) {
  using Reports = std::vector<std::vector<report::Report>>;
  Reports reports(files.size());
  std::atomic<bool> zero_hashes = false;
  util::ParallelFor(jobs, files.size(), [&](size_t index) {
    std::vector<report::Report>& file_reports = reports[index];
    file_reports = report::GetReports(files[index]);
    for (report::Report& report : file_reports) {
      report.severity = labels.UniqueLabelOfChecker(report.checker_name,
                                                    "severity", std::nullopt);
    }
    // Cppcheck run without CodeChecker writes "0" hashes.
    if (!file_reports.empty() &&
        std::ranges::all_of(file_reports, [](const report::Report& report) {
          return report.report_hash == "0";
        })) {
      kLog.Error("All bug hashes are 0 in the report file, which is no "
                 "longer supported. Please re-analyze the project using "
                 "CodeChecker!");
      zero_hashes = true;
    }
    kLog.Debug(std::format("[{}/{}] Parsed '{}' ...", index, files.size(),
                           files[index]));
  });
  if (zero_hashes) return Result<Reports>(Error{});
  return Result<Reports>(std::move(reports));
}

// Returns what the reports of `result_files` (with `reports` of each) refer
// to, and counts the result files and the kept reports in `statistics`.
// Of the reports with the same path hash only the first is kept; reports
// whose files changed since the analysis are not.
ReportedFiles CollectReportedFiles(
    const std::vector<std::string>& result_files,
    std::vector<std::vector<report::Report>> reports,
    report::Statistics& statistics) {
  ReportedFiles reported;
  std::set<std::string> path_hashes;
  for (size_t i = 0; i < result_files.size(); ++i) {
    statistics.AddResultFile();
    for (report::Report& report : reports[i]) {
      const std::set<std::string> changed = report::ChangedFiles(report);
      if (!changed.empty()) {
        reported.changed_files.insert(changed.begin(), changed.end());
        continue;
      }
      const std::set<std::string> files = report::OriginalFiles(report);
      reported.source_files.insert(files.begin(), files.end());
      reported.report_lines[report.file.original_path].insert(report.line);
      if (path_hashes.insert(report::GetReportPathHash(report)).second) {
        statistics.AddReport(report);
        ++reported.unique_report_count;
        reported
            .unique_reports[util::pypath::Dirname(result_files[i])]
                           [report.analyzer_name.value_or("")]
            .push_back(std::move(report));
      }
    }
  }
  return reported;
}

// Writes the reports of `unique_reports` into new plist files in `dir`,
// one per result folder and analyzer, and adds them to `folder_files`.
Result<void> WriteUniqueReports(const UniqueReports& unique_reports,
                                const std::string& dir,
                                FolderFiles& folder_files) {
  for (const auto& [folder, by_analyzer] : unique_reports) {
    for (const auto& [analyzer, reports] : by_analyzer) {
      const std::string name = analyzer.empty() ? "unknown" : analyzer;
      const std::string path = util::pypath::Join(
          {dir, std::format("{}-{}.plist", util::Uuid4(), name)});
      const Result<void> written = report::WritePlistFile(path, reports, name);
      if (!written.ok()) return written;
      kLog.Debug(
          std::format("Stored '{}' unique reports in {}.", name, path));
      folder_files[folder].insert(path);
    }
  }
  return Result<void>();
}

// Returns the main source files of `report_lines` that have review status
// comments at the report lines, read on several threads (Python:
// filter_source_files_with_comments()). Misspelled comments are logged.
Result<std::set<std::string>> FilesWithComments(
    const std::map<std::string, std::set<int>>& report_lines) {
  const std::vector<std::pair<std::string, std::set<int>>> jobs(
      report_lines.begin(), report_lines.end());
  std::vector<Result<bool>> has_comments(jobs.size(), Result<bool>(false));
  util::ParallelFor(platform::CpuCount(), jobs.size(), [&](size_t index) {
    const auto& [path, lines] = jobs[index];
    const Result<std::string> text = util::ReadTextFile(path);
    if (!text.ok()) {
      has_comments[index] = Result<bool>(text.error());
      return;
    }
    const common::SourceCommentScan scan =
        common::ScanSourceLineComments(text.value(), lines);
    if (!scan.misspelled.empty()) {
      kLog.Warning(std::format(
          "There are misspelled review status comments in {}", path));
    }
    for (const std::string& message : scan.misspelled) kLog.Warning(message);
    has_comments[index] = Result<bool>(scan.scanned_lines > 0);
  });
  std::set<std::string> files;
  for (size_t i = 0; i < jobs.size(); ++i) {
    if (!has_comments[i].ok()) {
      return Result<std::set<std::string>>(has_comments[i].error());
    }
    if (has_comments[i].value()) files.insert(jobs[i].first);
  }
  return Result<std::set<std::string>>(std::move(files));
}

// What MissingHashes() asks the server for.
enum class Missing {
  kContent,    // The source files it lacks (getMissingContentHashes).
  kBlameInfo,  // Their blame information (...ForBlameInfo).
};

// Returns which of the hashes of `file_hashes` the server of `results` asks
// for: the source files or the blame information it lacks, as `missing`
// says. None, without asking, if there is no hash.
Result<std::set<std::string>> MissingHashes(
    Missing missing, const std::map<std::string, std::string>& file_hashes,
    ResultsClient& results) {
  const bool content = missing == Missing::kContent;
  const std::string_view what =
      content ? "missing file content hashes"
              : "file content hashes which do not have blame information";
  kLog.Info(std::format("Get {} from the server...", what));
  std::set<std::string> unique_hashes;
  for (const auto& [path, hash] : file_hashes) unique_hashes.insert(hash);
  const std::vector<std::string> hashes(unique_hashes.begin(),
                                        unique_hashes.end());
  std::vector<std::string> asked;
  if (!hashes.empty()) {
    const Result<void> called = results.Call(
        content ? "getMissingContentHashes"
                : "getMissingContentHashesForBlameInfo",
        [&](codeCheckerDBAccessClient& client) {
          if (content) {
            client.getMissingContentHashes(asked, hashes);
          } else {
            client.getMissingContentHashesForBlameInfo(asked, hashes);
          }
        });
    if (!called.ok()) return Result<std::set<std::string>>(called.error());
  }
  kLog.Info(std::format("Get {} done.", what));
  return Result<std::set<std::string>>(
      std::set<std::string>(asked.begin(), asked.end()));
}

// Adds the files of `folder_files` to `zip` as
// "reports/<md5 of the folder>/<file name>"; a conf/ folder adds the files
// in it. Folders inside conf/ are left out (Python adds empty entries for
// them).
Result<void> AddReportFolders(const FolderFiles& folder_files,
                              util::ZipArchive& zip) {
  for (const auto& [folder, files] : folder_files) {
    const std::string folder_name = util::Md5Hex(folder);
    for (const std::string& path : files) {
      const std::string target = util::pypath::Join(
          {"reports", folder_name, util::pypath::Basename(path)});
      std::vector<std::pair<std::string, std::string>> entries;
      if (util::pypath::IsDir(path)) {
        for (const std::string& name : platform::ListDirectory(path)) {
          const std::string entry = util::pypath::Join({path, name});
          if (util::pypath::IsDir(entry)) continue;
          entries.emplace_back(util::pypath::Join({target, name}), entry);
        }
      } else {
        entries.emplace_back(target, path);
      }
      for (const auto& [name, source] : entries) {
        const Result<void> added =
            zip.AddFile(name, source, util::ZipArchive::Compression::kStored);
        if (!added.ok()) return added;
      }
    }
  }
  return Result<void>();
}

// Adds the blame information of the source files whose hashes are in
// `blame_hashes` to `zip` and counts them in `statistics`.
Result<void> AddBlameInfo(const std::map<std::string, std::string>& file_hashes,
                          const std::set<std::string>& blame_hashes,
                          util::ZipArchive& zip, ZipStatistics& statistics) {
  if (blame_hashes.empty()) return Result<void>();
  std::vector<std::string> files;
  for (const auto& [path, hash] : file_hashes) {
    if (blame_hashes.contains(hash)) files.push_back(path);
  }
  kLog.Info("Collecting blame information for source files...");
  // Python cannot import GitPython without the git program.
  if (!platform::Which("git", std::nullopt)) {
    kLog.Warning("Failed to collect blame information. Make sure Git is "
                 "installed on your system.");
    return Result<void>();
  }
  const Result<int> count = AssembleBlameInfo(files, zip);
  if (!count.ok()) return Result<void>(count.error());
  statistics.blame_files = count.value();
  kLog.Info(count.value() > 0 ? "Collecting blame information... Done."
                              : "No blame information found for source files.");
  return Result<void>();
}

// Writes the zip `zip_file`: the files of `folder_files`, the source files
// of `file_hashes` whose hashes are in `needed_hashes`, the blame
// information of those in `blame_hashes`, and content_hashes.json.
Result<void> WriteZip(const std::string& zip_file,
                      const FolderFiles& folder_files,
                      const std::map<std::string, std::string>& file_hashes,
                      const std::set<std::string>& needed_hashes,
                      const std::set<std::string>& blame_hashes,
                      ZipStatistics& statistics) {
  Result<util::ZipArchive> zip =
      util::ZipArchive::Open(zip_file, util::ZipArchive::Mode::kWrite);
  if (!zip.ok()) return Result<void>(zip.error());
  Result<void> added = AddReportFolders(folder_files, zip.value());
  if (!added.ok()) return added;
  for (const auto& [path, hash] : file_hashes) {
    if (!needed_hashes.contains(hash)) continue;
    kLog.Debug(std::format("File contents for '{}' needed by the server",
                           path));
    ++statistics.source_files;
    const std::string name = "root/" + std::string(util::LeftStrip(path, "/"));
    if (zip.value().Contains(name)) continue;
    added = zip.value().AddFile(name, path,
                                util::ZipArchive::Compression::kStored);
    if (!added.ok()) return added;
  }
  added = AddBlameInfo(file_hashes, blame_hashes, zip.value(), statistics);
  if (!added.ok()) return added;
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_val* hashes = yyjson_mut_obj(document.get());
  for (const auto& [path, hash] : file_hashes) {
    yyjson_mut_obj_add(hashes, util::MakeJsonString(document.get(), path),
                       util::MakeJsonString(document.get(), hash));
  }
  added = zip.value().AddText("content_hashes.json", util::DumpJson(hashes));
  if (!added.ok()) return added;
  return zip.value().Close();
}

// Prints the summary of the zip (Python: StorageZipStatistics.write()).
void WriteSummary(const ZipStatistics& statistics) {
  statistics.reports.WriteReportTables();
  const std::vector<std::vector<std::string>> rows = {
      {"Number of processed analyzer result files",
       std::to_string(statistics.reports.num_of_analyzer_result_files())},
      {"Number of analyzer reports",
       std::to_string(statistics.reports.num_of_reports())},
      {"Number of source files", std::to_string(statistics.source_files)},
      {"Number of source files with source code comments",
       std::to_string(statistics.source_files_with_comments)},
      {"Number of blame information files",
       std::to_string(statistics.blame_files)}};
  util::Write(util::Stream::kStdout,
              "\n----======== Summary ========----\n" +
                  util::twodim::ToTable(rows, {.show_header = false}) +
                  "\n----=================----\n");
}

// Fails, after logging why, if the product of `products` takes fewer
// reports than `report_count`.
Result<void> CheckReportLimit(int report_count, ProductClient& products) {
  Product product;
  const Result<void> called = products.Call(
      "getCurrentProduct",
      [&product](codeCheckerProductServiceClient& client) {
        client.getCurrentProduct(product);
      });
  if (!called.ok()) return called;
  if (report_count <= product.reportLimit) return Result<void>();
  kLog.Error(std::format(kReportLimitExceeded, product.reportLimit));
  return Result<void>(Error{});
}

// Replaces the content of `zip_file` with its zlib stream.
Result<void> CompressZip(const std::string& zip_file) {
  kLog.Info("Compressing report zip file...");
  const int64_t size = platform::FileSize(zip_file).value_or(0);
  const Result<std::string> compressed = util::ZlibCompressFile(zip_file);
  if (!compressed.ok()) return Result<void>(compressed.error());
  kLog.Info(std::format(
      "Compressing report zip file done ({} / {}).",
      common::FormatSize(static_cast<double>(size)),
      common::FormatSize(static_cast<double>(compressed.value().size()))));
  return Result<void>();
}

// Returns the SHA-256 of the content of each of `files`, by path.
Result<std::map<std::string, std::string>> HashSourceFiles(
    const std::set<std::string>& files) {
  using Hashes = std::map<std::string, std::string>;
  Hashes hashes;
  for (const std::string& path : files) {
    const Result<std::string> content = util::ReadFile(path);
    if (!content.ok()) return Result<Hashes>(content.error());
    hashes.emplace(path, *util::HexDigest("sha256", content.value()));
  }
  return Result<Hashes>(std::move(hashes));
}

// Adds to `needed_hashes` the hashes of the main source files of
// `reported` that are not needed yet but have review status comments at
// the report lines, and counts them in `statistics`. The server reads the
// comments from the source files, so it gets the files with comments even
// if it has their content.
Result<void> AddFilesWithComments(
    const ReportedFiles& reported,
    const std::map<std::string, std::string>& file_hashes,
    std::set<std::string>& needed_hashes, ZipStatistics& statistics) {
  kLog.Info("Collecting review comments ...");
  std::map<std::string, std::set<int>> known_files;
  for (const auto& [path, lines] : reported.report_lines) {
    if (!needed_hashes.contains(file_hashes.at(path))) {
      known_files.emplace(path, lines);
    }
  }
  const Result<std::set<std::string>> with_comments =
      FilesWithComments(known_files);
  if (!with_comments.ok()) return Result<void>(with_comments.error());
  for (const std::string& path : with_comments.value()) {
    needed_hashes.insert(file_hashes.at(path));
    ++statistics.source_files_with_comments;
  }
  kLog.Info("Collecting review comments done.");
  return Result<void>();
}

// The part of AssembleZip() after the reports were read, with the folder
// `unique_dir` for the result files without duplicates.
Result<void> BuildZip(const std::string& zip_file,
                      const std::string& unique_dir, ReportedFiles reported,
                      FolderFiles folder_files, ResultsClient& results,
                      ProductClient& products, ZipStatistics& statistics) {
  Result<void> done =
      WriteUniqueReports(reported.unique_reports, unique_dir, folder_files);
  if (!done.ok()) return done;
  if (!reported.changed_files.empty()) {
    report::DumpChangedFiles(reported.changed_files);
    return Result<void>(Error{});
  }
  if (reported.source_files.empty()) {
    kLog.Warning("There is no report to store. After uploading these results "
                 "the previous reports become resolved.");
  }
  const Result<std::map<std::string, std::string>> file_hashes =
      HashSourceFiles(reported.source_files);
  if (!file_hashes.ok()) return Result<void>(file_hashes.error());
  Result<std::set<std::string>> needed =
      MissingHashes(Missing::kContent, file_hashes.value(), results);
  if (!needed.ok()) return Result<void>(needed.error());
  const Result<std::set<std::string>> blame_hashes =
      MissingHashes(Missing::kBlameInfo, file_hashes.value(), results);
  if (!blame_hashes.ok()) return Result<void>(blame_hashes.error());
  done = AddFilesWithComments(reported, file_hashes.value(), needed.value(),
                              statistics);
  if (!done.ok()) return done;

  kLog.Info("Building report zip file...");
  done = WriteZip(zip_file, folder_files, file_hashes.value(), needed.value(),
                  blame_hashes.value(), statistics);
  if (!done.ok()) return done;
  kLog.Info(std::format("Building report zip file ({}) done.", zip_file));
  WriteSummary(statistics);
  // Fail early if the server would not take so many reports.
  done = CheckReportLimit(reported.unique_report_count, products);
  if (!done.ok()) return done;
  return CompressZip(zip_file);
}

}  // namespace

Result<void> AssembleZip(const std::vector<std::string>& inputs,
                         const std::string& zip_file,
                         const common::CheckerLabels& labels,
                         const std::string& temp_dir, int jobs,
                         ResultsClient& results, ProductClient& products) {
  FolderFiles folder_files;
  const std::vector<std::string> result_files =
      CollectResultFiles(inputs, folder_files);
  kLog.Debug(std::format("Processing {} report files ...",
                         result_files.size()));
  Result<std::vector<std::vector<report::Report>>> reports =
      ParseResultFiles(result_files, labels, jobs);
  if (!reports.ok()) return Result<void>(reports.error());
  kLog.Info("Processing report files done.");

  ZipStatistics statistics;
  ReportedFiles reported = CollectReportedFiles(
      result_files, std::move(reports).value(), statistics.reports);
  std::error_code error;
  const std::optional<std::string> unique_dir =
      platform::MakeTempDir(temp_dir, "tmp", "-unique-plists", &error);
  if (!unique_dir) {
    return Result<void>(util::OsError(
        error.value(), util::pypath::Join({temp_dir, "tmp-unique-plists"})));
  }
  const Result<void> built =
      BuildZip(zip_file, *unique_dir, std::move(reported),
               std::move(folder_files), results, products, statistics);
  platform::RemoveTree(*unique_dir);
  return built;
}

}  // namespace cc::client::store
