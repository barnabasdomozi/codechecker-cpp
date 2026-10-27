#include "server/database/db_cleanup.h"

#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/thrift/report_server_types.h"
#include "api/thrift/shared_types.h"
#include "common/checker_labels.h"
#include "platform/subprocess.h"
#include "server/database/connection.h"
#include "server/product.h"
#include "util/datetime.h"
#include "util/logging.h"
#include "util/parallel.h"
#include "util/result.h"
#include "util/strings.h"
#include "web/database_status.h"

namespace cc::server::db {
namespace {

constexpr util::Logger kLog("server");

// Runs the statements `sql` of one clean-up step in a transaction; logs
// "[<endpoint>] Failed to <what>: <error>" if a statement fails.
void RunStep(Connection& connection, std::string_view endpoint,
             std::string_view what, const std::vector<std::string>& sql,
             const std::vector<Param>& params = {}) {
  const Result<void> result = RunInTransaction(connection, [&] {
    for (const std::string& statement : sql) {
      Result<QueryResult> done = connection.Query(statement, params);
      if (!done.ok()) return Result<void>(done.error());
    }
    return Result<void>();
  });
  if (!result.ok()) {
    kLog.Error(std::format("[{}] Failed to {}: {}", endpoint, what,
                           result.error().message));
  }
}

// Returns the value of the severity named `name`; nullopt if there is none.
std::optional<int> SeverityValue(std::string_view name) {
  for (const auto& [value, severity] : _Severity_VALUES_TO_NAMES) {
    if (name == severity) return value;
  }
  return std::nullopt;
}

// Returns the severity that the label files give `checker` of `analyzer`
// where they have no entry for it: that of a similarly named checker if
// there is one without conflicts (Python: the new_sev_attempts part of
// upgrade_severity_levels()).
std::optional<std::string> SimilarCheckerSeverity(
    const common::CheckerLabels& labels, std::string_view endpoint,
    const std::string& analyzer, const std::string& checker) {
  std::map<std::string, std::string> attempts;  // Sorted, as Python sorts.
  for (const std::string& name :
       {analyzer + "." + checker, analyzer + "-" + checker,
        analyzer + "/" + checker}) {
    std::string severity =
        labels.UniqueLabelOfChecker(name, "severity", analyzer);
    if (severity != "UNSPECIFIED") attempts.emplace(name, std::move(severity));
  }
  if (attempts.empty()) {
    kLog.Debug(std::format("[{}] {}/{}: Keeping the old severity intact...",
                           endpoint, analyzer, checker));
    return std::nullopt;
  }
  const bool conflicting = std::ranges::any_of(
      attempts, [&attempts](const auto& attempt) {
        return attempt.second != attempts.begin()->second;
      });
  if (conflicting) {
    std::vector<std::string> items;
    for (const auto& [name, severity] : attempts) {
      items.push_back(
          std::format("({}, {})", util::Repr(name), util::Repr(severity)));
    }
    kLog.Error(std::format(
        "[{}] {}/{}: Multiple similar checkers WITH CONFLICTING SEVERITIES "
        "were found instead: [{}]",
        endpoint, analyzer, checker, util::Join(items, ", ")));
    return std::nullopt;
  }
  const auto& [name, severity] = *attempts.begin();
  kLog.Info(std::format(
      "[{}] {}/{}: Found similar checker '{}/{}' (severity: '{}'), using "
      "this for the upgrade.",
      endpoint, analyzer, checker, analyzer, name, severity));
  return severity;
}

// A new severity of a checker.
struct SeverityUpdate {
  int64_t checker_id;
  int severity;
};

// Updates the severities of the checkers to those of the label files
// (Python: upgrade_severity_levels()).
void UpgradeSeverityLevels(Connection& connection, std::string_view endpoint,
                           const common::CheckerLabels& labels) {
  std::vector<std::string> analyzers = labels.Analyzers();
  std::ranges::sort(analyzers);
  std::vector<SeverityUpdate> updates;
  for (const std::string& analyzer : analyzers) {
    Result<QueryResult> rows = connection.Query(
        "SELECT id, checker_name, severity FROM checkers "
        "WHERE analyzer_name = ?",
        {analyzer});
    if (!rows.ok()) {
      kLog.Error(std::format("[{}] Failed to upgrade severity levels: {}",
                             endpoint, rows.error().message));
      return;
    }
    for (const Row& row : rows.value().rows) {
      const std::string checker = row.Text(1);
      int old_value = static_cast<int>(row.Int(2));
      std::string old_name = "UNSPECIFIED";
      if (const auto found = _Severity_VALUES_TO_NAMES.find(old_value);
          found != _Severity_VALUES_TO_NAMES.end()) {
        old_name = found->second;
      } else {
        kLog.Error(std::format(
            "[{}] Checker '{}/{}' contains invalid severity {}, considering "
            "as if 'UNSPECIFIED' (0)!",
            endpoint, analyzer, checker, old_value));
        old_value = 0;
      }
      std::string new_name =
          labels.UniqueLabelOfChecker(checker, "severity", analyzer);
      if (old_name == new_name) continue;
      if (new_name == "UNSPECIFIED") {
        kLog.Warning(std::format(
            "[{}] Checker '{}/{}' (database severity: '{}' ({})) does not "
            "have a corresponding entry in the label config file.",
            endpoint, analyzer, checker, old_name, old_value));
        const std::optional<std::string> similar =
            SimilarCheckerSeverity(labels, endpoint, analyzer, checker);
        if (!similar || *similar == old_name) continue;
        new_name = *similar;
      }
      const std::optional<int> new_value = SeverityValue(new_name);
      if (!new_value) {
        kLog.Error(std::format("[{}] Unknown severity '{}' of checker '{}/{}'.",
                               endpoint, new_name, analyzer, checker));
        continue;
      }
      kLog.Info(std::format(
          "[{}] Upgrading the severity of checker '{}/{}' from '{}' ({}) to "
          "'{}' ({}).",
          endpoint, analyzer, checker, old_name, old_value, new_name,
          *new_value));
      updates.push_back({.checker_id = row.Int(0), .severity = *new_value});
    }
  }
  std::vector<std::string> statements;
  for (const SeverityUpdate& update : updates) {
    statements.push_back(
        std::format("UPDATE checkers SET severity = {} WHERE id = {}",
                    update.severity, update.checker_id));
  }
  RunStep(connection, endpoint, "upgrade severity levels", statements);
}

// Cleans the database of one product (Python: Product.cleanup_run_db()).
void CleanupProduct(Connection& connection, std::string_view endpoint,
                    const common::CheckerLabels& labels) {
  kLog.Info(std::format("[{}] Garbage collection started...", endpoint));
  RunStep(connection, endpoint, "remove expired run locks",
          {"DELETE FROM run_locks WHERE locked_at < ?"},
          {util::FormatDatabaseTime(util::LocalNow() -
                                    kRunLockTimeoutSeconds *
                                        util::kMicrosecondsPerSecond)});
  RunStep(connection, endpoint, "remove unused files",
          {"DELETE FROM files WHERE id NOT IN "
           "(SELECT file_id FROM report_path_data_files GROUP BY file_id)",
           "DELETE FROM file_contents WHERE content_hash NOT IN "
           "(SELECT content_hash FROM files "
           "UNION SELECT content_hash FROM analysis_info_files)"});
  RemoveUnusedComments(connection, endpoint);
  RemoveUnusedAnalysisInfo(connection, endpoint);
  RunStep(connection, endpoint, "remove checker sets",
          {"DELETE FROM checker_set WHERE id NOT IN "
           "(SELECT checker_set_id FROM analysis_info "
           "WHERE checker_set_id IS NOT NULL GROUP BY checker_set_id)"});
  UpgradeSeverityLevels(connection, endpoint, labels);
  kLog.Info(std::format("[{}] Garbage collection finished.", endpoint));
}

}  // namespace

void RemoveUnusedComments(Connection& connection, std::string_view endpoint) {
  RunStep(connection, endpoint, "remove dangling comments",
          {"DELETE FROM comments WHERE id IN "
           "(SELECT comments.id FROM comments LEFT OUTER JOIN reports "
           "ON comments.bug_hash = reports.bug_id WHERE reports.id IS NULL)"});
}

void RemoveUnusedAnalysisInfo(Connection& connection,
                              std::string_view endpoint) {
  RunStep(connection, endpoint, "remove dangling analysis info",
          {"DELETE FROM analysis_info WHERE id IN "
           "(SELECT analysis_info.id FROM analysis_info "
           "LEFT OUTER JOIN run_history_analysis_info "
           "ON run_history_analysis_info.analysis_info_id = analysis_info.id "
           "LEFT OUTER JOIN report_analysis_info "
           "ON report_analysis_info.analysis_info_id = analysis_info.id "
           "WHERE run_history_analysis_info.analysis_info_id IS NULL "
           "AND report_analysis_info.analysis_info_id IS NULL)"});
}

void CleanupProductDatabases(const std::vector<Product::Record>& products,
                             const std::string& workspace,
                             const common::CheckerLabels& labels) {
  if (products.empty()) return;
  const int jobs = std::min(static_cast<int>(products.size()),
                            platform::CpuCount());
  kLog.Info(std::format(
      "Performing database cleanup using {} concurrent jobs...", jobs));
  // Why the clean-up of each product failed; "" if it did not.
  std::vector<std::string> failures(products.size());
  util::ParallelFor(jobs, products.size(), [&](size_t index) {
    Product product(products[index], workspace);
    product.Connect(/*init=*/false);
    std::string failure;
    if (product.db_status() != DBStatus::OK) {
      failure = std::format(
          "Cleanup not attempted, database status is \"{}\"",
          web::DatabaseStatusMessage(product.db_status()));
    } else if (Result<Connection*> connection = product.Connection();
               connection.ok()) {
      CleanupProduct(*connection.value(), product.endpoint(), labels);
    } else {
      failure = connection.error().message;
    }
    product.Teardown();
    failures[index] = std::move(failure);
  });
  std::vector<std::string> lines;
  for (size_t i = 0; i < products.size(); ++i) {
    if (failures[i].empty()) continue;
    lines.push_back(
        std::format("'{}' ({})", products[i].endpoint, failures[i]));
  }
  if (lines.empty()) return;
  kLog.Error(std::format(
      "Failed to perform automatic cleanup on {} products! Earlier logs might "
      "contain additional detailed reasoning.\n\t* {}",
      lines.size(), util::Join(lines, "\n\t* ")));
}

}  // namespace cc::server::db
