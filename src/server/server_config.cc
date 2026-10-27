#include "server/server_config.h"

#include <stdint.h>
#include <yyjson.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/util.h"
#include "platform/subprocess.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "web/env.h"

namespace cc::server {
namespace {

constexpr util::Logger kLog("server");

// Replaces the "$SECRET:<name>$" and "$ENV:<name>$" strings in and under
// `value` (Python: resolve_variables() in SessionManager.__get_config_dict()).
// `secrets` is the secrets file's object, or null.
Result<void> ResolveVariables(yyjson_mut_doc* doc, yyjson_mut_val* value,
                              yyjson_val* secrets,
                              const std::string& secrets_file) {
  static const util::Regex& kSecret =
      *new util::Regex(R"(^\$SECRET:[a-zA-Z0-9_-]+\$$)");
  static const util::Regex& kEnv =
      *new util::Regex(R"(^\$ENV:[a-zA-Z0-9_-]+\$$)");
  // Returns the resolved value of the string `text`, null if it is not a
  // variable, or the error for an unresolvable one.
  auto resolve = [&](std::string_view text) -> Result<yyjson_mut_val*> {
    const bool secret = kSecret.Search(text).has_value();
    const bool env = kEnv.Search(text).has_value();
    if (!secret && !env) return Result<yyjson_mut_val*>(nullptr);
    // Python: v.split(':')[1][:-1].
    const size_t colon = text.find(':');
    std::string_view name = text.substr(colon + 1);
    name = name.substr(0, name.find(':'));
    name.remove_suffix(1);
    if (secret) {
      if (yyjson_val* resolved = util::JsonGet(secrets, name)) {
        return Result<yyjson_mut_val*>(yyjson_val_mut_copy(doc, resolved));
      }
    } else if (const std::optional<std::string> resolved =
                   platform::GetEnv(std::string(name))) {
      return Result<yyjson_mut_val*>(util::MakeJsonString(doc, *resolved));
    }
    if (secret && !util::pypath::Exists(secrets_file)) {
      kLog.Error(std::format(
          "Secrets were used in server configuration file, but {} does not "
          "exist!",
          secrets_file));
    }
    return Result<yyjson_mut_val*>(Error{.message = std::format(
        "Variable '{}' could not be resolved in server configuration file.",
        text)});
  };

  // Returns what replaces the member or array item `item`: its resolved
  // value if it is a variable, null to keep it. The variables under it are
  // resolved in place.
  auto resolve_item = [&](yyjson_mut_val* item) -> Result<yyjson_mut_val*> {
    if (yyjson_mut_is_str(item)) return resolve(util::JsonString(item));
    Result<void> inner = ResolveVariables(doc, item, secrets, secrets_file);
    if (!inner.ok()) return Result<yyjson_mut_val*>(inner.error());
    return Result<yyjson_mut_val*>(nullptr);
  };

  if (yyjson_mut_is_obj(value)) {
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
    while (yyjson_mut_val* key = yyjson_mut_obj_iter_next(&iter)) {
      Result<yyjson_mut_val*> resolved =
          resolve_item(yyjson_mut_obj_iter_get_val(key));
      if (!resolved.ok()) return Result<void>(resolved.error());
      if (resolved.value()) {
        yyjson_mut_obj_replace(value, key, resolved.value());
      }
    }
  } else if (yyjson_mut_is_arr(value)) {
    for (size_t i = 0; i < yyjson_mut_arr_size(value); ++i) {
      Result<yyjson_mut_val*> resolved =
          resolve_item(yyjson_mut_arr_get(value, i));
      if (!resolved.ok()) return Result<void>(resolved.error());
      if (resolved.value()) {
        yyjson_mut_arr_replace(value, i, resolved.value());
      }
    }
  }
  return Result<void>();
}

// Returns the configuration in `config_file` with its variables resolved
// (Python: SessionManager.__get_config_dict()).
Result<util::JsonDocument> ReadConfig(const std::string& config_file,
                                      const std::string& secrets_file) {
  using Config = Result<util::JsonDocument>;
  kLog.Debug(config_file);
  std::optional<util::JsonDocument> config = common::LoadJson(config_file);
  yyjson_val* root =
      config ? yyjson_doc_get_root(config->get()) : nullptr;
  if (!yyjson_is_obj(root) || yyjson_obj_size(root) == 0) {
    return Config(Error{
        .message = "Server configuration file was invalid, or empty."});
  }
  web::CheckFileOwnerReadWrite(config_file);

  kLog.Debug(secrets_file);
  std::optional<util::JsonDocument> secrets;
  if (util::pypath::Exists(secrets_file)) {
    secrets = common::LoadJson(secrets_file);
    web::CheckFileOwnerReadWrite(secrets_file);
  }
  yyjson_val* secrets_root =
      secrets ? yyjson_doc_get_root(secrets->get()) : nullptr;

  const util::MutableJsonDocument copy(
      yyjson_doc_mut_copy(config->get(), nullptr));
  Result<void> resolved =
      ResolveVariables(copy.get(), yyjson_mut_doc_get_root(copy.get()),
                       secrets_root, secrets_file);
  if (!resolved.ok()) return Config(resolved.error());
  return Config(util::JsonDocument(yyjson_mut_doc_imut_copy(copy.get(),
                                                            nullptr)));
}

// Returns `value` as an integer if it is a JSON number; nullopt otherwise.
std::optional<int64_t> JsonNumber(yyjson_val* value) {
  if (!yyjson_is_num(value)) return std::nullopt;
  return util::JsonToInt(value);
}

// The numbers of worker processes of the server.
struct WorkerCounts {
  int api;   // API handler processes.
  int task;  // Task worker processes.
};

// Returns the numbers of API handler and task worker processes in `config`
// (Python: get_worker_processes()).
WorkerCounts WorkerProcesses(yyjson_val* config) {
  const int default_count = platform::CpuCount();
  auto count = [config, default_count](std::string_view key) {
    yyjson_val* value = util::JsonGet(config, key);
    return value ? JsonNumber(value) : std::optional<int64_t>(default_count);
  };
  std::optional<int64_t> api = count("worker_processes");
  std::optional<int64_t> task = count("background_worker_processes");
  if (!api || *api <= 0) {
    kLog.Warning(std::format(
        "Number of worker processes can not be negative! Default value will "
        "be used: {}",
        default_count));
    api = default_count;
  }
  if (!task || *task <= 0) {
    kLog.Warning(std::format(
        "Number of task worker processes can not be negative! Default value "
        "will be used: {}",
        *api));
    task = api;
  }
  return {.api = static_cast<int>(*api), .task = static_cast<int>(*task)};
}

// Returns whether the object `config` has a member `method` with a true
// `enabled` (Python: 'method_x' in auth_config and
// auth_config['method_x'].get('enabled')).
bool MethodEnabled(yyjson_val* config, std::string_view method) {
  return util::JsonTruthy(util::JsonGet(util::JsonGet(config, method),
                                        "enabled"));
}

// Returns the strings in the array `value`.
std::vector<std::string> Strings(yyjson_val* value) {
  std::vector<std::string> strings;
  yyjson_arr_iter iter = yyjson_arr_iter_with(value);
  while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
    strings.push_back(util::JsonString(item));
  }
  return strings;
}

// Reads the `authentication` object `config` and makes the start-up checks
// of SessionManager.__init__(): methods that cannot be used are turned off,
// and authentication is turned off if no method is left, unless
// `force_authentication`.
Result<AuthenticationConfig> ReadAuthentication(yyjson_val* config,
                                                bool force_authentication) {
  using Auth = Result<AuthenticationConfig>;
  AuthenticationConfig auth;
  auth.enabled = util::JsonTruthy(util::JsonGet(config, "enabled"));
  if (force_authentication) {
    kLog.Debug("Authentication was force-enabled.");
    auth.enabled = true;
  }
  if (util::JsonGet(config, "soft_expire")) {
    kLog.Debug(
        "Found deprecated argument 'soft_expire' in "
        "server_config.authentication.");
  }
  if (yyjson_val* super_user = util::JsonGet(config, "super_user")) {
    auth.super_user = util::JsonString(super_user);
  }
  auth.failed_auth_message =
      util::JsonString(util::JsonGet(config, "failed_auth_message"));
  auth.session_lifetime =
      JsonNumber(util::JsonGet(config, "session_lifetime"));
  auth.refresh_time = JsonNumber(util::JsonGet(config, "refresh_time"));
  auth.logins_until_cleanup =
      JsonNumber(util::JsonGet(config, "logins_until_cleanup"));
  if (yyjson_val* max = util::JsonGet(
          config, "max_pers_auth_token_expiration_length")) {
    auth.max_token_expiration = JsonNumber(max).value_or(365);
  }

  yyjson_val* dictionary = util::JsonGet(config, "method_dictionary");
  auth.dictionary_enabled = MethodEnabled(config, "method_dictionary");
  auth.dictionary_auths = Strings(util::JsonGet(dictionary, "auths"));
  yyjson_obj_iter groups =
      yyjson_obj_iter_with(util::JsonGet(dictionary, "groups"));
  while (yyjson_val* user = yyjson_obj_iter_next(&groups)) {
    auth.dictionary_groups.push_back(
        {.user = util::JsonString(user),
         .groups = Strings(yyjson_obj_iter_get_val(user))});
  }

  yyjson_val* regex_groups = util::JsonGet(config, "regex_groups");
  auth.regex_groups_enabled =
      util::JsonTruthy(util::JsonGet(regex_groups, "enabled"));
  yyjson_obj_iter regex_iter =
      yyjson_obj_iter_with(util::JsonGet(regex_groups, "groups"));
  while (yyjson_val* name = yyjson_obj_iter_next(&regex_iter)) {
    AuthenticationConfig::RegexGroup group = {.name = util::JsonString(name),
                                              .patterns = {}};
    for (const std::string& pattern :
         Strings(yyjson_obj_iter_get_val(name))) {
      Result<util::Regex> regex = util::Regex::Compile(pattern);
      if (!regex.ok()) return Auth(regex.error());
      group.patterns.push_back(std::move(regex).value());
    }
    auth.regex_groups.push_back(std::move(group));
  }

  if (!auth.enabled) {
    return Auth(std::move(auth));
  }
  bool found_method = auth.dictionary_enabled;
  // LDAP and PAM are reported like the Python server reports a missing
  // python-ldap or python-pam (libraries.md, "LDAP and PAM").
  if (MethodEnabled(config, "method_ldap")) {
    kLog.Warning(
        "LDAP authentication was enabled but prerequisites are NOT installed "
        "on the system... Disabling LDAP authentication.");
  }
  if (MethodEnabled(config, "method_pam")) {
    kLog.Warning(
        "PAM authentication was enabled but prerequisites are NOT installed "
        "on the system... Disabling PAM authentication.");
  }
  if (MethodEnabled(config, "method_oauth")) {
    kLog.Warning(
        "OAuth authentication is not supported by this server... Disabling "
        "OAuth authentication.");
  }
  if (!found_method) {
    if (force_authentication) {
      kLog.Warning(
          "Authentication was manually enabled, but no valid authentication "
          "backends are configured... The server will only allow the "
          "superuser (root) access.");
    } else {
      kLog.Warning(
          "Authentication is enabled but no valid authentication backends "
          "are configured... Falling back to no authentication.");
      auth.enabled = false;
    }
  }
  return Auth(std::move(auth));
}

}  // namespace

Result<ServerConfig> ServerConfig::Load(const std::string& config_file,
                                        const std::string& secrets_file,
                                        bool force_authentication) {
  Result<util::JsonDocument> document = ReadConfig(config_file, secrets_file);
  if (!document.ok()) return Result<ServerConfig>(document.error());
  yyjson_val* root = yyjson_doc_get_root(document.value().get());
  const auto [api, task] = WorkerProcesses(root);
  Result<yyjson_val*> authentication =
      util::JsonSubscript(root, "authentication");
  if (!authentication.ok()) return Result<ServerConfig>(authentication.error());
  Result<AuthenticationConfig> auth =
      ReadAuthentication(authentication.value(), force_authentication);
  if (!auth.ok()) return Result<ServerConfig>(auth.error());
  return Result<ServerConfig>(ServerConfig(std::move(document).value(),
                                           std::move(auth).value(), api,
                                           task));
}

Result<void> ServerConfig::Reload(const std::string& config_file,
                                  const std::string& secrets_file) {
  Result<util::JsonDocument> document = ReadConfig(config_file, secrets_file);
  if (!document.ok()) return Result<void>(document.error());
  return Result<void>();
}

bool ServerConfig::KeepaliveEnabled() const {
  yyjson_val* keepalive =
      util::JsonGet(yyjson_doc_get_root(document_.get()), "keepalive");
  return util::JsonTruthy(util::JsonGet(keepalive, "enabled"));
}

int64_t ServerConfig::KeepaliveValue(const char* key) const {
  yyjson_val* keepalive =
      util::JsonGet(yyjson_doc_get_root(document_.get()), "keepalive");
  return JsonNumber(util::JsonGet(keepalive, key)).value_or(0);
}

std::optional<int64_t> ServerConfig::MaxRunCount() const {
  return JsonNumber(
      util::JsonGet(yyjson_doc_get_root(document_.get()), "max_run_count"));
}

std::optional<std::string> ServerConfig::AnalysisStatisticsDir() const {
  yyjson_val* store =
      util::JsonGet(yyjson_doc_get_root(document_.get()), "store");
  return util::JsonOptionalString(
      util::JsonGet(store, "analysis_statistics_dir"));
}

std::optional<int64_t> ServerConfig::StoreLimit(const char* key) const {
  yyjson_val* store =
      util::JsonGet(yyjson_doc_get_root(document_.get()), "store");
  return JsonNumber(util::JsonGet(util::JsonGet(store, "limit"), key));
}

std::optional<int64_t> ServerConfig::FailureZipSizeLimit() const {
  return StoreLimit("failure_zip_size");
}

std::optional<int64_t> ServerConfig::CompilationDatabaseSizeLimit() const {
  return StoreLimit("compilation_database_size");
}

int64_t ServerConfig::KeepaliveIdle() const { return KeepaliveValue("idle"); }

int64_t ServerConfig::KeepaliveInterval() const {
  return KeepaliveValue("interval");
}

int64_t ServerConfig::KeepaliveMaxProbe() const {
  return KeepaliveValue("max_probe");
}

}  // namespace cc::server
