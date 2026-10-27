// The package data files that the CodeCheckerService answers from (Python:
// the parts of codechecker_web's webserver_context that
// codechecker_server/api/report_server.py uses).

#ifndef CC_SERVER_PACKAGE_DATA_H_
#define CC_SERVER_PACKAGE_DATA_H_

#include <string>
#include <vector>

#include "common/checker_labels.h"
#include "common/guidelines.h"
#include "util/pyre.h"
#include "util/result.h"

namespace cc::server {

// An entry of config/git_commit_urls.json: the commits of the remote URLs
// that `regex` matches are at `url`, with "$<group>" replaced by the named
// groups of the match and "$commit" by the commit hash.
struct GitCommitUrl {
  util::Regex regex;
  std::string url;
};

// A system comment kind of config/system_comment_kinds.json: the HTML text
// of the comments of `kind`, with "{0}", "{1}", ... for their arguments.
struct SystemCommentKind {
  std::string kind;
  std::string text;
};

// The package data of the server. Move-only; read-only after loading, so it
// can be used from several threads.
struct PackageData {
  common::CheckerLabels checker_labels;
  common::Guidelines guidelines;
  // In the order of config/system_comment_kinds.json.
  std::vector<SystemCommentKind> system_comments;
  std::vector<GitCommitUrl> git_commit_urls;
};

// Reads the package data from the package's config folder. Fails with the
// message of the exception that stops the Python server.
Result<PackageData> LoadPackageData();

}  // namespace cc::server

#endif  // CC_SERVER_PACKAGE_DATA_H_
