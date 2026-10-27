// Port of codechecker_server/api/report_server.py: the CodeCheckerService of
// a product: runs, reports, review statuses, comments, source components,
// cleanup plans and filter presets (the cmd-*.md pages and web-ui.md in
// docs/reference).
//
// Storing is not ported yet: massStoreRun(), massStoreRunAsynchronous() and
// the calls only `store` makes (getMissingContentHashes(),
// getMissingContentHashesForBlameInfo(), allowsStoringAnalysisStatistics(),
// getAnalysisStatisticsLimits(), storeAnalysisStatistics()) fail with
// GENERAL.

#ifndef CC_SERVER_API_REPORT_HANDLER_H_
#define CC_SERVER_API_REPORT_HANDLER_H_

#include <stdint.h>

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "api/ordered_map.h"
#include "api/thrift/codeCheckerDBAccess.h"
#include "api/thrift/report_server_types.h"
#include "api/thrift/shared_types.h"
#include "server/database/connection.h"
#include "server/database/sql.h"
#include "server/server_context.h"
#include "util/result.h"

namespace cc::server::api {

// Answers the CodeCheckerService calls of one request, which has a product.
// Errors are thrown as RequestFailed.
class ReportHandler final : public codeCheckerDBAccessIf {
 public:
  explicit ReportHandler(const Request& request) : request_(request) {}

  ReportHandler(const ReportHandler&) = delete;
  ReportHandler& operator=(const ReportHandler&) = delete;

  // Arguments the client did not send arrive as their default values: 0 for
  // a limit, an offset or an id, an empty list, a struct with no field set.
  // These are taken as Python's None.
  void getRunData(RunDataList& result, const RunFilter& run_filter,
                  int64_t limit, int64_t offset,
                  const RunSortMode& sort_mode) override;
  int64_t storeFilterPreset(const FilterPreset& preset) override;
  int64_t renameFilterPreset(int64_t id, const std::string& name) override;
  void getFilterPreset(FilterPreset& result, int64_t id) override;
  int64_t deleteFilterPreset(int64_t id) override;
  void listFilterPreset(std::vector<FilterPreset>& result) override;
  int64_t getRunCount(const RunFilter& run_filter) override;
  void getCheckCommand(std::string& result, int64_t run_history_id,
                       int64_t run_id) override;
  void getAnalysisInfo(std::vector<AnalysisInfo>& result,
                       const AnalysisInfoFilter& filter, int64_t limit,
                       int64_t offset) override;
  void getRunHistory(RunHistoryDataList& result,
                     const std::vector<int64_t>& run_ids, int64_t limit,
                     int64_t offset,
                     const RunHistoryFilter& run_history_filter) override;
  int64_t getRunHistoryCount(
      const std::vector<int64_t>& run_ids,
      const RunHistoryFilter& run_history_filter) override;
  void getDiffResultsHash(
      std::vector<std::string>& result, const std::vector<int64_t>& run_ids,
      const std::vector<std::string>& report_hashes, DiffType::type diff_type,
      const std::vector<DetectionStatus::type>& skip_detection_statuses,
      const std::vector<int64_t>& tag_ids) override;
  void getReport(ReportData& result, int64_t report_id) override;
  // An empty `sort_types` is taken as None: by severity, descending.
  void getRunResults(ReportDataList& result,
                     const std::vector<int64_t>& run_ids, int64_t limit,
                     int64_t offset, const std::vector<SortMode>& sort_types,
                     const ReportFilter& report_filter,
                     const CompareData& cmp_data, bool get_details) override;
  void getReportAnnotations(std::vector<std::string>& result,
                            const std::vector<int64_t>& run_ids,
                            const ReportFilter& report_filter,
                            const CompareData& cmp_data) override;
  void getRunReportCounts(RunReportCounts& result,
                          const std::vector<int64_t>& run_ids,
                          const ReportFilter& report_filter, int64_t limit,
                          int64_t offset) override;
  int64_t getRunResultCount(const std::vector<int64_t>& run_ids,
                            const ReportFilter& report_filter,
                            const CompareData& cmp_data) override;
  int64_t getFailedFilesCount(const std::vector<int64_t>& run_ids) override;
  void getFailedFiles(FailedFiles& result,
                      const std::vector<int64_t>& run_ids) override;
  void getReportDetails(ReportDetails& result, int64_t report_id) override;
  void getSourceFileData(SourceFileData& result, int64_t file_id,
                         bool file_content, Encoding::type encoding) override;
  void getBlameInfo(BlameInfo& result, int64_t file_id) override;
  void getLinesInSourceFileContents(
      cc::api::OrderedMap<int64_t, cc::api::OrderedMap<int64_t, std::string>>&
          result,
      const LinesInFilesRequestedList& lines_in_files_requested,
      Encoding::type encoding) override;
  bool isReviewStatusChangeDisabled() override;
  bool changeReviewStatus(int64_t report_id, ReviewStatus::type status,
                          const std::string& message) override;
  void getReviewStatusRules(ReviewStatusRules& result,
                            const ReviewStatusRuleFilter& filter,
                            const ReviewStatusRuleSortMode& sort_mode,
                            int64_t limit, int64_t offset) override;
  int64_t getReviewStatusRulesCount(
      const ReviewStatusRuleFilter& filter) override;
  bool removeReviewStatusRules(const ReviewStatusRuleFilter& filter) override;
  bool addReviewStatusRule(const std::string& report_hash,
                           ReviewStatus::type status,
                           const std::string& message) override;
  void getComments(CommentDataList& result, int64_t report_id) override;
  int64_t getCommentCount(int64_t report_id) override;
  bool addComment(int64_t report_id, const CommentData& comment) override;
  bool updateComment(int64_t comment_id,
                     const std::string& new_message) override;
  bool removeComment(int64_t comment_id) override;
  void getCheckerDoc(std::string& result,
                     const std::string& checker_id) override;
  void getCheckerLabels(std::vector<std::vector<std::string>>& result,
                        const std::vector<::Checker>& checkers) override;
  void getGuidelineRules(GuidelineRules& result,
                         const std::vector<Guideline>& guidelines) override;
  void getPackageVersion(std::string& result) override;
  bool removeRunResults(const std::vector<int64_t>& run_ids) override;
  bool removeRunReports(const std::vector<int64_t>& run_ids,
                        const ReportFilter& report_filter,
                        const CompareData& cmp_data) override;
  bool removeRun(int64_t run_id, const RunFilter& run_filter) override;
  bool updateRunData(int64_t run_id, const std::string& new_run_name) override;
  void getSuppressFile(std::string& result) override;
  void getSeverityCounts(cc::api::OrderedMap<Severity::type, int64_t>& result,
                         const std::vector<int64_t>& run_ids,
                         const ReportFilter& report_filter,
                         const CompareData& cmp_data) override;
  void getCheckerMsgCounts(cc::api::OrderedMap<std::string, int64_t>& result,
                           const std::vector<int64_t>& run_ids,
                           const ReportFilter& report_filter,
                           const CompareData& cmp_data, int64_t limit,
                           int64_t offset) override;
  void getReportStatusCounts(
      cc::api::OrderedMap<ReportStatus::type, int64_t>& result,
      const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
      const CompareData& cmp_data) override;
  void getReviewStatusCounts(
      cc::api::OrderedMap<ReviewStatus::type, int64_t>& result,
      const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
      const CompareData& cmp_data) override;
  void getDetectionStatusCounts(
      cc::api::OrderedMap<DetectionStatus::type, int64_t>& result,
      const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
      const CompareData& cmp_data) override;
  void getFileCounts(cc::api::OrderedMap<std::string, int64_t>& result,
                     const std::vector<int64_t>& run_ids,
                     const ReportFilter& report_filter,
                     const CompareData& cmp_data, int64_t limit,
                     int64_t offset) override;
  void getFileCountsSummary(
      cc::api::OrderedMap<std::string,
                          cc::api::OrderedMap<std::string, int64_t>>& result,
      const std::vector<int64_t>& run_ids, const ReportFilter& report_filter,
      const CompareData& cmp_data, int64_t limit, int64_t offset) override;
  void getCheckerCounts(CheckerCounts& result,
                        const std::vector<int64_t>& run_ids,
                        const ReportFilter& report_filter,
                        const CompareData& cmp_data, int64_t limit,
                        int64_t offset) override;
  void getCheckerStatusVerificationDetails(
      CheckerStatusVerificationDetails& result,
      const std::vector<int64_t>& run_ids,
      const ReportFilter& report_filter) override;
  void getRunHistoryTagCounts(RunTagCounts& result,
                              const std::vector<int64_t>& run_ids,
                              const ReportFilter& report_filter,
                              const CompareData& cmp_data, int64_t limit,
                              int64_t offset) override;
  void getAnalyzerNameCounts(cc::api::OrderedMap<std::string, int64_t>& result,
                             const std::vector<int64_t>& run_ids,
                             const ReportFilter& report_filter,
                             const CompareData& cmp_data, int64_t limit,
                             int64_t offset) override;
  bool addSourceComponent(const std::string& name, const std::string& value,
                          const std::string& description) override;
  void getSourceComponents(
      SourceComponentDataList& result,
      const std::vector<std::string>& component_filter) override;
  bool removeSourceComponent(const std::string& name) override;
  void getMissingContentHashes(
      std::vector<std::string>& result,
      const std::vector<std::string>& file_hashes) override;
  void getMissingContentHashesForBlameInfo(
      std::vector<std::string>& result,
      const std::vector<std::string>& file_hashes) override;
  int64_t massStoreRun(const std::string& run_name, const std::string& tag,
                       const std::string& version, const std::string& zipfile,
                       bool force,
                       const std::vector<std::string>& trim_path_prefixes,
                       const std::string& description) override;
  void massStoreRunAsynchronous(::TaskToken& result,
                                const std::string& zipfile_blob,
                                const SubmittedRunOptions& store_opts) override;
  bool allowsStoringAnalysisStatistics() override;
  void getAnalysisStatisticsLimits(
      cc::api::OrderedMap<StoreLimitKind::type, int64_t>& result) override;
  bool storeAnalysisStatistics(const std::string& run_name,
                               const std::string& zipfile) override;
  void getAnalysisStatistics(AnalyzerStatisticsData& result, int64_t run_id,
                             int64_t run_history_id) override;
  void exportData(ExportData& result, const RunFilter& run_filter) override;
  bool importData(const ExportData& export_data) override;
  int64_t addCleanupPlan(const std::string& name,
                         const std::string& description,
                         int64_t due_date) override;
  bool updateCleanupPlan(int64_t id, const std::string& name,
                         const std::string& description,
                         int64_t due_date) override;
  void getCleanupPlans(CleanupPlans& result,
                       const CleanupPlanFilter& filter) override;
  bool removeCleanupPlan(int64_t cleanup_plan_id) override;
  bool closeCleanupPlan(int64_t cleanup_plan_id) override;
  bool reopenCleanupPlan(int64_t cleanup_plan_id) override;
  bool setCleanupPlan(int64_t cleanup_plan_id,
                      const std::vector<std::string>& report_hashes) override;
  bool unsetCleanupPlan(int64_t cleanup_plan_id,
                        const std::vector<std::string>& report_hashes) override;

 private:
  // Returns the connection to the product's database; throws DATABASE if it
  // cannot be opened.
  db::Connection& Db();

  // Runs `sql` on the product's database; throws DATABASE if it fails.
  db::QueryResult Run(const db::Sql& sql);

  // Throws UNAUTHORIZED unless the caller has one of `permissions` in the
  // product (Python: __require_permission()).
  void Require(std::initializer_list<Permission::type> permissions);

  // Returns the user name of the caller; "Anonymous" for a guest (Python:
  // _get_username()).
  std::string User() const;

  // Returns the bug paths, notes and comments of the reports `report_ids`
  // (Python: get_report_details()).
  std::map<int64_t, ReportDetails> GetReportDetails(
      const std::vector<int64_t>& report_ids);

  // Returns the comment `message` of `kind` as the API shows it: a system
  // comment in the HTML of its kind, a user comment HTML-escaped (Python:
  // get_comment_msg()).
  std::string CommentMessage(int64_t kind, const std::string& message) const;

  // Inserts a comment of the caller on the reports of `report_hash`, at
  // `date` (Python: __add_comment()).
  void AddComment(const std::string& report_hash, const std::string& message,
                  int64_t kind, int64_t date);

  // Sets the review status rule of `report_hash` and the reports it applies
  // to, with a system comment, at `date` (now if nullopt). Changes nothing
  // if the status and the message are the same. Runs in the caller's
  // transaction (Python: _setReviewStatus()).
  void SetReviewStatus(const std::string& report_hash,
                       ReviewStatus::type status, const std::string& message,
                       std::optional<int64_t> date);

  // Fails with the names of the runs if a store holds the lock of one of
  // `run_ids` (of any run if empty) (Python: check_remove_runs_lock()).
  Result<void> CheckRemoveRunsLock(const std::vector<int64_t>& run_ids);

  // Removes the runs `run_ids`, each in its own transaction, and returns
  // whether there were any (Python: the body of removeRun()). Fails if a run
  // is locked or the database fails.
  Result<bool> RemoveRuns(const std::vector<int64_t>& run_ids);

  // Removes the reports of `run_ids` that `report_filter` and `cmp_data`
  // select; returns false, after logging why, if that fails (Python: the
  // body of removeRunReports()).
  bool RemoveReports(const std::vector<int64_t>& run_ids,
                     const ReportFilter& report_filter,
                     const CompareData& cmp_data);

  // Returns the query of the review status rules of `filter`, with their
  // report counts, ordered by `sort_mode` unless it is null (Python:
  // get_rs_rule_query()).
  db::Select ReviewStatusRuleQuery(const ReviewStatusRuleFilter& filter,
                                   const ReviewStatusRuleSortMode* sort_mode);

  // Returns the cleanup plan `id`, with its name in column 1; throws
  // DATABASE if there is none (Python: get_cleanup_plan()).
  db::Row CleanupPlanRow(int64_t id);

  // Changes the cached number of runs of the product by `change` (Python:
  // Product.set_cached_run_data()).
  Result<void> ChangeCachedRunCount(int64_t change);

  // Returns whether the client's API version is at least 6.`minor`.
  bool ClientAtLeast(int minor) const;

  Request request_;
};

}  // namespace cc::server::api

#endif  // CC_SERVER_API_REPORT_HANDLER_H_
