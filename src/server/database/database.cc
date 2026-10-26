#include "server/database/database.h"

#include <charconv>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "platform/subprocess.h"
#include "server/database/connection.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::server::db {
namespace {

// Returns `text` with the characters SQLAlchemy escapes in user names and
// passwords written as %XX (Python: sqlalchemy's _sqla_url_quote()).
std::string Quote(std::string_view text) {
  std::string quoted;
  for (const char c : text) {
    if (c == ':' || c == '@' || c == '/') {
      quoted += std::format("%{:02X}", static_cast<unsigned char>(c));
    } else {
      quoted += c;
    }
  }
  return quoted;
}

// Returns the name of the PostgreSQL driver in connection strings (Python:
// host_check.get_postgresql_driver_name()). The port always uses libpq, the
// library behind psycopg2.
std::string PostgresqlDriver() {
  return platform::GetEnv("CODECHECKER_DB_DRIVER").value_or("psycopg2");
}

}  // namespace

std::optional<ConnectionUrl> ParseConnectionUrl(std::string_view url) {
  // The pattern of sqlalchemy.engine.url._parse_url().
  static const util::Regex& kPattern = *new util::Regex(
      R"((?P<name>[\w\+]+)://)"
      R"((?:(?P<username>[^:/]*)(?::(?P<password>[^@]*))?@)?)"
      R"((?:(?:\[(?P<ipv6host>[^/\?]+)\]|(?P<ipv4host>[^/:\?]+))?)"
      R"((?::(?P<port>[^/\?]*))?)?)"
      R"((?:/(?P<database>[^\?]*))?(?:\?(?P<query>.*))?)");
  const std::optional<util::RegexMatch> match = kPattern.Match(url);
  if (!match) return std::nullopt;
  ConnectionUrl parsed = {.driver = std::string(*match->Group("name"))};
  if (const std::optional<std::string_view> user = match->Group("username")) {
    parsed.username = util::PercentDecode(*user);
  }
  if (const std::optional<std::string_view> password =
          match->Group("password")) {
    parsed.password = util::PercentDecode(*password);
  }
  if (const std::optional<std::string_view> host = match->Group("ipv6host")) {
    parsed.host = std::string(*host);
  } else if (const std::optional<std::string_view> host =
                 match->Group("ipv4host")) {
    parsed.host = std::string(*host);
  }
  if (const std::optional<std::string_view> port = match->Group("port")) {
    int value = 0;
    if (std::from_chars(port->data(), port->data() + port->size(), value)
            .ec == std::errc()) {
      parsed.port = value;
    }
  }
  parsed.database = std::string(match->Group("database").value_or(""));
  parsed.query = std::string(match->Group("query").value_or(""));
  return parsed;
}

std::string FormatConnectionUrl(const ConnectionUrl& url, bool hide_password) {
  std::string text = url.driver + "://";
  if (url.username) {
    text += Quote(*url.username);
    if (url.password) {
      text += ":" + (hide_password ? std::string("***") : Quote(*url.password));
    }
    text += "@";
  }
  if (url.host) text += util::BracketIpv6Host(*url.host);
  if (url.port) text += std::format(":{}", *url.port);
  text += "/" + url.database;
  if (!url.query.empty()) text += "?" + url.query;
  return text;
}

ConnectionUrl ResolveSqliteRelativePath(ConnectionUrl url,
                                        std::string_view workspace) {
  if (url.driver.find("sqlite") != std::string::npos &&
      !url.database.empty() && !url.database.starts_with('/')) {
    url.database = util::pypath::Join({workspace, url.database});
  }
  return url;
}

Database Database::Sqlite(std::string path) {
  return Database(std::move(path));
}

Database Database::Postgresql(PostgresqlParams params) {
  return Database(std::move(params));
}

Result<Database> Database::FromConnectionString(std::string_view url,
                                                std::string_view workspace) {
  const std::optional<ConnectionUrl> parsed = ParseConnectionUrl(url);
  if (parsed && parsed->driver.find("postgresql") != std::string::npos) {
    return Result<Database>(Postgresql(
        {.host = parsed->host.value_or(""),
         .port = parsed->port.value_or(5432),
         .user = parsed->username.value_or(""),
         .password = parsed->password,
         .database = parsed->database}));
  }
  if (parsed && parsed->driver.find("sqlite") != std::string::npos) {
    // Python: SQLServer.from_cmdline_args() with the workspace of the
    // server context.
    const std::string path =
        !workspace.empty() && !parsed->database.starts_with('/')
            ? util::pypath::Join({workspace, parsed->database})
            : util::pypath::Abspath(parsed->database);
    return Result<Database>(Sqlite(path));
  }
  return Result<Database>(Error{
      .message = std::format("Unsupported database connection: '{}'", url)});
}

ConnectionUrl Database::Url() const {
  if (const std::string* path = std::get_if<std::string>(&location_)) {
    return {.driver = "sqlite+pysqlite", .database = *path};
  }
  const PostgresqlParams& params = std::get<PostgresqlParams>(location_);
  const std::string driver = PostgresqlDriver();
  return {.driver = "postgresql+" + driver,
          .username = params.user,
          .password = params.password,
          .host = params.host,
          .port = params.port,
          .database = params.database,
          .query = driver == "psycopg2" ? "client_encoding=utf8" : ""};
}

std::string Database::ConnectionString() const {
  return FormatConnectionUrl(Url(), /*hide_password=*/false);
}

std::string Database::SafeConnectionString() const {
  return FormatConnectionUrl(Url(), /*hide_password=*/true);
}

std::string Database::Location() const {
  if (const std::string* path = std::get_if<std::string>(&location_)) {
    return *path;
  }
  const PostgresqlParams& params = std::get<PostgresqlParams>(location_);
  return std::format("{}:{}/{}", params.host, params.port, params.database);
}

Result<std::unique_ptr<Connection>> Database::Open() const {
  if (const std::string* path = std::get_if<std::string>(&location_)) {
    return OpenSqlite(*path);
  }
  return OpenPostgresql(std::get<PostgresqlParams>(location_));
}

Result<Connection*> LazyConnection::Get() {
  if (!connection_) {
    Result<std::unique_ptr<Connection>> opened = database_.Open();
    if (!opened.ok()) return Result<Connection*>(opened.error());
    connection_ = std::move(opened).value();
  }
  return Result<Connection*>(connection_.get());
}

Result<QueryResult> LazyConnection::Query(std::string_view sql,
                                          const std::vector<Param>& params) {
  Result<Connection*> connection = Get();
  if (!connection.ok()) return Result<QueryResult>(connection.error());
  return connection.value()->Query(sql, params);
}

Result<std::optional<std::string>> LazyConnection::QueryValue(
    std::string_view sql, const std::vector<Param>& params) {
  Result<Connection*> connection = Get();
  if (!connection.ok()) {
    return Result<std::optional<std::string>>(connection.error());
  }
  return db::QueryValue(*connection.value(), sql, params);
}

}  // namespace cc::server::db
