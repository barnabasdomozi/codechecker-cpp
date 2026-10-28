#include "common/source_code_comment_handler.h"

#include <stddef.h>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/logging.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::common {
namespace {

constexpr util::Logger kLog("report-converter");

constexpr std::string_view kMarkers[] = {
    "codechecker_suppress", "codechecker_false_positive",
    "codechecker_intentional", "codechecker_confirmed"};

using Comments = std::vector<SourceCodeComment>;

// Returns line `number` of `lines`, or "" if there is no such line (Python:
// util.get_linef()).
std::string_view LineAt(const std::vector<std::string_view>& lines,
                        int number) {
  if (number < 1 || static_cast<size_t>(number) > lines.size()) return "";
  return lines[number - 1];
}

// Parses the text of one comment, its comment characters removed (Python:
// __process_source_line_comment()); nullopt if it is not a review status
// comment.
std::optional<SourceCodeComment> ProcessSourceLineComment(
    std::string_view comment) {
  static const util::Regex& kPattern = *new util::Regex(
      R"(^\s*(?P<status>codechecker_suppress|codechecker_false_positive|)"
      R"(codechecker_intentional|codechecker_confirmed))"
      R"(\s*\[\s*(?P<checkers>[^\]]*)\s*\]\s*(?P<comment>.*)$)");
  static const util::Regex& kCheckerName = *new util::Regex(R"([^,\s]+)");
  const std::string formatted =
      util::Join(util::SplitWhitespace(comment), " ");
  const std::optional<util::RegexMatch> match = kPattern.Match(formatted);
  if (!match) return std::nullopt;
  SourceCodeComment result = {.checkers = {},
                              .message = "WARNING! source code comment is "
                                         "missing",
                              .status = "false_positive",
                              .line = ""};
  const std::string_view checkers = *match->Group("checkers");
  if (checkers == "all") {
    result.checkers.emplace_back("all");
  } else {
    for (const util::RegexMatch& name :
         kCheckerName.FindAll(util::Strip(checkers))) {
      const std::string_view text = *name.Group(0);
      // The names are a set in Python.
      if (std::ranges::find(result.checkers, text) == result.checkers.end()) {
        result.checkers.emplace_back(text);
      }
    }
  }
  if (const std::string_view message = *match->Group("comment");
      !message.empty()) {
    result.message = message;
  }
  const std::string_view status = *match->Group("status");
  if (status == "codechecker_intentional") {
    result.status = "intentional";
  } else if (status == "codechecker_confirmed") {
    result.status = "confirmed";
  }
  return result;
}

// Returns the text of the comment made of `lines` (top-down), without the
// comment characters.
std::string CommentText(const std::vector<std::string_view>& lines,
                        const std::string& joined) {
  if (util::Strip(lines.front()).starts_with("//")) {
    return util::Replace(joined, "//", "");
  }
  std::vector<std::string> parts;
  for (const std::string_view line : lines) {
    std::string part = util::Replace(
        util::Replace(util::Strip(line), "/*", ""), "*/", "");
    if (part.starts_with('*')) part.erase(0, 1);
    parts.push_back(std::move(part));
  }
  return std::string(util::Strip(util::Join(parts, " ")));
}

// Returns the review status comments above line `bug_line` (Python:
// get_source_line_comments()). It walks up from the line before, while the
// lines are comments or inside a C style comment, and starts a new comment
// at each line with a marker.
Result<Comments> GetSourceLineComments(
    const std::vector<std::string_view>& lines, int bug_line) {
  int line_number = bug_line - 1;
  Comments comments;
  if (line_number < 1) return Result<Comments>(std::move(comments));
  std::vector<std::string_view> current;  // Bottom-up.
  bool cstyle_end_found = false;
  while (true) {
    const std::string_view line = LineAt(lines, line_number);
    const std::string_view stripped = util::Strip(line);
    const bool cstyle_start = stripped.find("/*") != std::string_view::npos;
    const bool cstyle_end = stripped.find("*/") != std::string_view::npos;
    if (!stripped.starts_with("//") && !cstyle_start && !cstyle_end &&
        !cstyle_end_found) {
      break;
    }
    cstyle_end_found = cstyle_end_found || cstyle_end;
    current.push_back(line);
    if (std::ranges::any_of(kMarkers, [line](std::string_view marker) {
          return line.find(marker) != std::string_view::npos;
        })) {
      const std::vector<std::string_view> top_down(current.rbegin(),
                                                   current.rend());
      std::vector<std::string> parts(top_down.begin(), top_down.end());
      const std::string joined = util::Join(parts, " ");
      std::optional<SourceCodeComment> comment =
          ProcessSourceLineComment(CommentText(top_down, joined));
      if (!comment) {
        return Result<Comments>(Error{
            .message = std::format("misspelled review status comment @{}: {}",
                                   line_number, util::Strip(joined))});
      }
      comment->line = joined;
      comments.push_back(std::move(*comment));
      current.clear();
    }
    if (line_number <= 0) break;
    --line_number;
    if (cstyle_start) break;
  }
  return Result<Comments>(std::move(comments));
}

}  // namespace

Result<std::vector<SourceCodeComment>> FilterSourceLineComments(
    std::string_view text, int bug_line, std::string_view checker_name) {
  Result<Comments> comments =
      GetSourceLineComments(util::SplitLines(text), bug_line);
  if (!comments.ok()) return comments;
  Comments matching;
  for (const SourceCodeComment& comment : comments.value()) {
    for (const std::string& name : comment.checkers) {
      if (checker_name.find(name) != std::string_view::npos || name == "all") {
        matching.push_back(comment);
      }
    }
  }
  if (matching.empty()) {
    kLog.Debug("No source code comments are found for checker " +
               std::string(checker_name));
  } else if (matching.size() > 1) {
    kLog.Debug(std::format("Multiple source code comment can be found for "
                           "'{}' checker at line {}.",
                           checker_name, bug_line));
  }
  return Result<Comments>(std::move(matching));
}

SourceCommentScan ScanSourceLineComments(std::string_view text,
                                         const std::set<int>& lines) {
  SourceCommentScan scan;
  if (text.find("codechecker_") == std::string_view::npos) return scan;
  const std::vector<std::string_view> text_lines = util::SplitLines(text);
  for (const int line : lines) {
    const Result<Comments> comments = GetSourceLineComments(text_lines, line);
    if (comments.ok()) {
      ++scan.scanned_lines;
    } else {
      scan.misspelled.push_back(comments.error().message);
    }
  }
  return scan;
}

}  // namespace cc::common
