#include "client/credentials/user_credentials.h"

#include <stdint.h>
#include <yyjson.h>

#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "common/util.h"
#include "platform/filesystem.h"
#include "platform/locked_file.h"
#include "util/file.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/unicode.h"
#include "web/env.h"

namespace cc::client::credentials {
namespace {

constexpr util::Logger kLog("system");

// Returns the credentials of the "credentials" object `credentials` by
// "host:port", or "host" if the key has no port: protocols and product
// names are removed (Python: simplify_credentials()). Keys of another form
// are logged and left out.
std::map<std::string, std::string> SimplifyCredentials(
    yyjson_val* credentials) {
  static const util::Regex& kHostEntry = *new util::Regex(
      R"(^(?P<protocol>http[s]?://)*(?P<host>[^:\/\s]+):*(?P<port>\d+)*)"
      R"(/*(?P<product>\w+)*$)");
  std::map<std::string, std::string> simplified;
  yyjson_obj_iter iter = yyjson_obj_iter_with(credentials);
  while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
    const std::string entry = util::JsonString(key);
    const std::optional<util::RegexMatch> match = kHostEntry.Match(entry);
    if (!match) {
      kLog.Warning(std::format("Not a valid host entry: {}.", entry));
      continue;
    }
    std::string host_port(*match->Group("host"));
    if (const std::optional<std::string_view> port = match->Group("port")) {
      host_port += ":" + std::string(*port);
    }
    simplified.insert_or_assign(
        std::move(host_port),
        util::JsonString(yyjson_obj_iter_get_val(key)));
  }
  return simplified;
}

// Opens the session file `path` under an exclusive lock, as Python does;
// the file is created if it is missing (Python opens it with mode 'a+' or
// 'w'). The error message is the text of Python's OSError.
Result<platform::LockedFile> OpenSessionFile(const std::string& path) {
  const Result<void> created = util::AppendFile(path, "");
  if (!created.ok()) return Result<platform::LockedFile>(created.error());
  std::error_code error;
  std::optional<platform::LockedFile> file = platform::LockedFile::Open(
      path, platform::LockedFile::Lock::kExclusive, &error);
  if (!file) {
    return Result<platform::LockedFile>(util::OsError(error.value(), path));
  }
  return Result<platform::LockedFile>(std::move(*file));
}

// Returns the tokens of the session file `path`, which is created if it is
// missing and reset to no tokens if it is not valid JSON.
Result<std::map<std::string, std::string>> LoadTokens(
    const std::string& path) {
  using Tokens = std::map<std::string, std::string>;
  Result<platform::LockedFile> file = OpenSessionFile(path);
  if (!file.ok()) return Result<Tokens>(file.error());
  const std::optional<std::string> content = file.value().Read();
  Result<util::JsonDocument> document =
      util::ParseJson(util::DecodeUtf8Ignore(content.value_or("")));
  Tokens tokens;
  if (!document.ok()) {
    if (const std::error_code replaced =
            file.value().Replace(R"({"tokens": {}})")) {
      return Result<Tokens>(util::OsError(replaced.value(), path));
    }
    platform::SetFileMode(path, 0600);
    return Result<Tokens>(std::move(tokens));
  }
  web::CheckFileOwnerReadWrite(path);
  kLog.Debug("Found session information for these hosts:");
  yyjson_val* saved =
      util::JsonGet(yyjson_doc_get_root(document.value().get()), "tokens");
  yyjson_obj_iter iter = yyjson_obj_iter_with(saved);
  while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
    kLog.Debug("  " + util::JsonString(key));
    tokens.insert_or_assign(util::JsonString(key),
                            util::JsonString(yyjson_obj_iter_get_val(key)));
  }
  return Result<Tokens>(std::move(tokens));
}

}  // namespace

Result<UserCredentials> UserCredentials::Load() {
  kLog.Debug("Loading clientside session config.");
  UserCredentials result;
  const std::string password_file = web::PasswordFile();
  kLog.Info(
      std::format("Checking local passwords or tokens in {}", password_file));
  const std::string mistyped_file = util::pypath::Join(
      {platform::HomeDirectory(), ".codechecker.password.json"});
  if (util::pypath::Exists(password_file)) {
    web::CheckFileOwnerReadWrite(password_file);
    const std::optional<util::JsonDocument> document =
        common::LoadJson(password_file);
    yyjson_val* root =
        document ? yyjson_doc_get_root(document->get()) : nullptr;
    yyjson_val* credentials = util::JsonGet(root, "credentials");
    if (!yyjson_is_obj(credentials)) {
      return Result<UserCredentials>(Error{
          .message = std::format("Invalid password file '{}': it has no "
                                 "\"credentials\" object.",
                                 password_file)});
    }
    result.credentials_ = SimplifyCredentials(credentials);
    if (result.credentials_.empty()) {
      kLog.Info("No saved tokens.");
    } else {
      kLog.Debug("Tokens or passwords were found for these hosts:");
      for (const auto& [host, auth_string] : result.credentials_) {
        kLog.Debug(std::format("  user '{}' host '{}'",
                               auth_string.substr(0, auth_string.find(':')),
                               host));
      }
    }
    if (yyjson_val* autologin = util::JsonGet(root, "client_autologin")) {
      result.autologin_ = util::JsonTruthy(autologin);
    }
  } else if (util::pypath::Exists(mistyped_file)) {
    kLog.Warning(std::format("Typo in file name! Rename '{}' to '{}'.",
                             mistyped_file, password_file));
  } else {
    kLog.Info("Password file not found.");
  }

  result.session_file_ = web::SessionFile();
  kLog.Info("Checking for local valid sessions.");
  Result<std::map<std::string, std::string>> tokens =
      LoadTokens(result.session_file_);
  if (!tokens.ok()) return Result<UserCredentials>(tokens.error());
  result.tokens_ = std::move(tokens).value();
  return Result<UserCredentials>(std::move(result));
}

std::optional<std::string> UserCredentials::GetToken(std::string_view host,
                                                     int64_t port) const {
  const auto it = tokens_.find(std::format("{}:{}", host, port));
  if (it == tokens_.end()) return std::nullopt;
  return it->second;
}

std::optional<std::string> UserCredentials::GetAuthString(
    std::string_view host, int64_t port) const {
  for (const std::string& key :
       {std::format("{}:{}", host, port), std::string(host),
        std::format("*:{}", port), std::string("*")}) {
    // Python: `if not ret`, so an empty credential counts as none.
    const auto it = credentials_.find(key);
    if (it != credentials_.end() && !it->second.empty()) return it->second;
  }
  return std::nullopt;
}

Result<void> UserCredentials::SaveToken(std::string_view host, int64_t port,
                                        const std::string& token) {
  tokens_.insert_or_assign(std::format("{}:{}", host, port), token);
  // json.dump(..., indent=2, sort_keys=True): the map is sorted.
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* saved = yyjson_mut_obj(doc);
  for (const auto& [key, value] : tokens_) {
    yyjson_mut_obj_add(saved, util::MakeJsonString(doc, key),
                       util::MakeJsonString(doc, value));
  }
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, root, "tokens", saved);
  Result<platform::LockedFile> file = OpenSessionFile(session_file_);
  if (!file.ok()) return Result<void>(file.error());
  if (const std::error_code replaced =
          file.value().Replace(util::DumpJsonIndent(root, 2))) {
    return Result<void>(util::OsError(replaced.value(), session_file_));
  }
  return Result<void>();
}

}  // namespace cc::client::credentials
