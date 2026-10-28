#include "client/store/blame_info.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/subprocess.h"
#include "util/datetime.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/parallel.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"
#include "util/zip.h"

namespace cc::client::store {
namespace {

constexpr util::Logger kLog("system");

// The data of a commit that the blame information shows.
struct Commit {
  std::string author_name;
  std::string author_email;
  int64_t committed_date = 0;  // Unix time.
  std::string message;
  std::string committed_datetime;  // As Python's str() of a datetime.
};

// A range of lines of the file that `commit` last changed.
struct BlameRange {
  int64_t from = 0;
  int64_t to = 0;
  std::string commit;
};

// The blame of a file.
struct Blame {
  std::vector<BlameRange> ranges;
  std::vector<std::string> shas;  // The commits, in the order first seen.
  std::map<std::string, Commit> commits;  // By sha.
};

// The parts of a commit object that GitPython reads lazily.
struct CommitObject {
  std::string message;
  std::string committer_time_zone;  // For example "+0200".
};

// Returns the stdout of `git args` run in the folder `dir`, with `input` on
// stdin; nullopt if it fails, which is logged at debug level with `file`.
std::optional<std::string> RunGit(const std::string& dir,
                                  std::vector<std::string> args,
                                  const std::string& file,
                                  std::optional<std::string> input =
                                      std::nullopt) {
  args.insert(args.begin(), "git");
  const platform::RunResult result = platform::RunProcess(
      args, {.cwd = dir, .input = std::move(input)});
  if (result.start_error != 0 || result.return_code != 0) {
    kLog.Debug(std::format("Failed to get blame information for {}: {}", file,
                           util::Strip(result.err)));
    return std::nullopt;
  }
  return result.out;
}

// Returns the first line of `text`, without its line end.
std::string FirstLine(std::string_view text) {
  return std::string(text.substr(0, text.find('\n')));
}

// Returns the Unix time `seconds` in the time zone `offset` ("+0200") as
// Python's str() of a datetime with that zone writes it (GitPython:
// committed_datetime).
std::string FormatCommitTime(int64_t seconds, std::string_view offset) {
  int64_t offset_minutes = 0;
  if (offset.size() == 5) {
    const std::optional<int64_t> hours = util::ParseInt(offset.substr(1, 2));
    const std::optional<int64_t> minutes = util::ParseInt(offset.substr(3, 2));
    offset_minutes = hours.value_or(0) * 60 + minutes.value_or(0);
    if (offset[0] == '-') offset_minutes = -offset_minutes;
  }
  const int64_t local =
      (seconds + offset_minutes * 60) * util::kMicrosecondsPerSecond;
  const int64_t size = offset_minutes < 0 ? -offset_minutes : offset_minutes;
  return std::format("{}{}{:02}:{:02}", util::FormatPythonTime(local),
                     offset_minutes < 0 ? '-' : '+', size / 60, size % 60);
}

// Returns the commit objects `shas` of the repository at `dir`, by sha.
std::map<std::string, CommitObject> ReadCommitObjects(
    const std::string& dir, const std::vector<std::string>& shas,
    const std::string& file) {
  std::map<std::string, CommitObject> objects;
  const std::optional<std::string> output = RunGit(
      dir, {"cat-file", "--batch"}, file, util::Join(shas, "\n") + "\n");
  if (!output) return objects;
  // Each object is "<sha> commit <size>\n<size bytes>\n".
  const std::string_view text = *output;
  size_t position = 0;
  for (const std::string& sha : shas) {
    const size_t header_end = text.find('\n', position);
    if (header_end == std::string_view::npos) break;
    const std::vector<std::string> header =
        util::Split(text.substr(position, header_end - position), " ");
    const std::optional<int64_t> size =
        header.size() == 3 ? util::ParseInt(header[2]) : std::nullopt;
    if (!size) break;
    const std::string_view object = text.substr(header_end + 1, *size);
    position = header_end + 1 + *size + 1;
    // The headers end at the first empty line; the message follows.
    const size_t headers_end = object.find("\n\n");
    CommitObject& parsed = objects[sha];
    for (const std::string_view line :
         util::SplitLines(object.substr(0, headers_end))) {
      if (line.starts_with("committer ")) {
        parsed.committer_time_zone =
            std::string(util::Strip(line.substr(line.rfind(' ') + 1)));
      }
    }
    if (headers_end != std::string_view::npos) {
      parsed.message = util::DecodeUtf8Replace(object.substr(headers_end + 2));
    }
  }
  return objects;
}

// Returns the branch HEAD tracks, as GitPython's str() of the remote
// reference ("origin/main", or "None" without one); the HEAD commit if no
// branch is checked out; nullopt if there is none.
std::optional<std::string> TrackingBranch(const std::string& dir,
                                          const std::string& file) {
  const std::optional<std::string> head =
      RunGit(dir, {"symbolic-ref", "-q", "HEAD"}, file);
  if (!head) {
    const std::optional<std::string> commit =
        RunGit(dir, {"rev-parse", "HEAD"}, file);
    if (!commit) return std::nullopt;
    return FirstLine(*commit);
  }
  constexpr std::string_view kHeads = "refs/heads/";
  std::string branch = FirstLine(*head);
  if (branch.starts_with(kHeads)) branch.erase(0, kHeads.size());
  const std::optional<std::string> remote = RunGit(
      dir, {"config", "--get", std::format("branch.{}.remote", branch)}, file);
  const std::optional<std::string> merge = RunGit(
      dir, {"config", "--get", std::format("branch.{}.merge", branch)}, file);
  if (!remote || !merge) return "None";
  // GitPython: the name of the merged branch is its path without the first
  // two parts.
  std::string merged = FirstLine(*merge);
  if (!merged.starts_with(kHeads)) merged = std::string(kHeads) + merged;
  const std::vector<std::string> parts = util::Split(merged, "/");
  const std::vector<std::string> name(parts.begin() + 2, parts.end());
  return std::format("{}/{}", FirstLine(*remote), util::Join(name, "/"));
}

// Reads the property line `line` of a commit of `git blame -p` into
// `commit`; other properties are ignored.
void ReadCommitProperty(std::string_view line, Commit& commit) {
  const size_t space = line.find(' ');
  const std::string_view tag = line.substr(0, space);
  std::string_view value;
  if (space != std::string_view::npos) value = line.substr(space + 1);
  if (tag == "author") {
    commit.author_name = util::DecodeUtf8Replace(value);
  } else if (tag == "author-mail") {
    if (value.starts_with('<')) value.remove_prefix(1);
    if (value.ends_with('>')) value.remove_suffix(1);
    commit.author_email = util::DecodeUtf8Replace(value);
  } else if (tag == "committer-time") {
    commit.committed_date = util::ParseInt(value).value_or(0);
  }
}

// Returns the blame in the output of `git blame -p --incremental`
// (Python: Repo.blame_incremental()). Each entry is "<sha> <original line>
// <line> <count>"; the first entry of a commit is followed by its
// properties, and every entry ends with a "filename" line.
Blame ParseIncrementalBlame(std::string_view output) {
  Blame blame;
  // Whether the lines read are inside an entry, before its "filename" line.
  bool in_entry = false;
  // The commit the property lines of the entry belong to; null if the
  // commit was seen in an earlier entry. Points into `blame.commits`.
  Commit* new_commit = nullptr;
  for (const std::string& text : util::Split(output, "\n")) {
    if (in_entry) {
      if (text.starts_with("filename ")) {
        in_entry = false;
      } else if (new_commit != nullptr) {
        ReadCommitProperty(text, *new_commit);
      }
      continue;
    }
    const std::vector<std::string> entry = util::SplitWhitespace(text);
    if (entry.size() != 4) continue;
    const std::optional<int64_t> line = util::ParseInt(entry[2]);
    const std::optional<int64_t> count = util::ParseInt(entry[3]);
    if (!line || !count) continue;
    const std::string& sha = entry[0];
    const bool seen = blame.commits.contains(sha);
    Commit& commit = blame.commits[sha];
    if (!seen) blame.shas.push_back(sha);
    new_commit = seen ? nullptr : &commit;
    in_entry = true;
    blame.ranges.push_back(
        {.from = *line, .to = *line + *count - 1, .commit = sha});
  }
  return blame;
}

// Returns the blame information JSON of `blame` (the dictionary of Python's
// __get_blame_info(), written by json.dumps()).
std::string BlameInfoJson(const std::optional<std::string>& tracking_branch,
                          const std::optional<std::string>& remote_url,
                          const Blame& blame) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  const auto add_string = [doc](yyjson_mut_val* object, const char* key,
                                std::string_view value) {
    yyjson_mut_obj_add_val(doc, object, key, util::MakeJsonString(doc, value));
  };
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  add_string(root, "version", "v1");
  yyjson_mut_obj_add_val(doc, root, "tracking_branch",
                         util::MakeJsonOptionalString(doc, tracking_branch));
  yyjson_mut_obj_add_val(doc, root, "remote_url",
                         util::MakeJsonOptionalString(doc, remote_url));
  yyjson_mut_val* commits = yyjson_mut_obj(doc);
  for (const std::string& sha : blame.shas) {
    const Commit& commit = blame.commits.at(sha);
    yyjson_mut_val* author = yyjson_mut_obj(doc);
    add_string(author, "name", commit.author_name);
    add_string(author, "email", commit.author_email);
    yyjson_mut_val* value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, value, "author", author);
    add_string(value, "summary", FirstLine(commit.message));
    add_string(value, "message", commit.message);
    add_string(value, "committed_datetime", commit.committed_datetime);
    yyjson_mut_obj_add(commits, util::MakeJsonString(doc, sha), value);
  }
  yyjson_mut_obj_add_val(doc, root, "commits", commits);
  yyjson_mut_val* ranges = yyjson_mut_arr(doc);
  for (const BlameRange& range : blame.ranges) {
    yyjson_mut_val* item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, item, "from", range.from);
    yyjson_mut_obj_add_int(doc, item, "to", range.to);
    add_string(item, "commit", range.commit);
    yyjson_mut_arr_append(ranges, item);
  }
  yyjson_mut_obj_add_val(doc, root, "blame", ranges);
  return util::DumpJson(root);
}

// Returns the blame information of `file` as JSON text; nullopt if the file
// is not committed in a Git repository (Python: __get_blame_info()).
std::optional<std::string> GetBlameInfo(const std::string& file) {
  const std::string real_path = util::pypath::Realpath(file);
  const std::string dir = util::pypath::Dirname(real_path);
  if (!RunGit(dir, {"rev-parse", "--show-toplevel"}, file)) {
    return std::nullopt;
  }
  if (platform::RunProcess({"git", "check-ignore", real_path},
                           {.cwd = dir,
                            .stdout_mode = platform::Output::kDevNull,
                            .stderr_mode = platform::Output::kDevNull})
          .return_code == 0) {
    kLog.Debug(std::format("File {} is an ignored file", file));
    return std::nullopt;
  }
  const std::optional<std::string> tracking_branch =
      TrackingBranch(dir, file);
  std::optional<std::string> remote_url;
  if (const std::optional<std::string> urls =
          RunGit(dir, {"remote", "get-url", "--all", "origin"}, file)) {
    remote_url = FirstLine(*urls);
  }
  const std::optional<std::string> head =
      RunGit(dir, {"rev-parse", "HEAD"}, file);
  if (!head) return std::nullopt;
  const std::optional<std::string> output = RunGit(
      dir, {"blame", "-p", "--incremental", FirstLine(*head), "--", real_path},
      file);
  if (!output) return std::nullopt;
  Blame blame = ParseIncrementalBlame(*output);
  for (auto& [sha, object] : ReadCommitObjects(dir, blame.shas, file)) {
    Commit& commit = blame.commits.at(sha);
    commit.message = std::move(object.message);
    commit.committed_datetime =
        FormatCommitTime(commit.committed_date, object.committer_time_zone);
  }
  kLog.Debug(std::format("Collected blame info for {}", file));
  return BlameInfoJson(tracking_branch, remote_url, blame);
}

}  // namespace

Result<int> AssembleBlameInfo(const std::vector<std::string>& files,
                              util::ZipArchive& zip) {
  std::vector<std::optional<std::string>> blame_infos(files.size());
  util::ParallelFor(platform::CpuCount(), files.size(),
                    [&files, &blame_infos](size_t index) {
                      blame_infos[index] = GetBlameInfo(files[index]);
                    });
  int count = 0;
  for (size_t i = 0; i < files.size(); ++i) {
    if (!blame_infos[i]) continue;
    const std::string name =
        "blame/" + std::string(util::LeftStrip(files[i], "/"));
    const Result<void> added = zip.AddText(name, std::move(*blame_infos[i]));
    if (!added.ok()) return Result<int>(added.error());
    ++count;
  }
  return Result<int>(count);
}

}  // namespace cc::client::store
