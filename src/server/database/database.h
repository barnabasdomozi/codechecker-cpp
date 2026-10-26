// Port of codechecker_server/database/database.py: where a database is
// (an SQLite file or a PostgreSQL database), its SQLAlchemy connection
// string, and opening it (server-database.md §2).

#ifndef CC_SERVER_DATABASE_DATABASE_H_
#define CC_SERVER_DATABASE_DATABASE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "server/database/connection.h"
#include "util/result.h"

namespace cc::server::db {

// The parts of an SQLAlchemy URL
// "<driver>://<user>:<password>@<host>:<port>/<database>?<query>" (Python:
// sqlalchemy.engine.url.URL).
struct ConnectionUrl {
  std::string driver = {};  // For example "sqlite+pysqlite".
  std::optional<std::string> username = {};
  std::optional<std::string> password = {};
  std::optional<std::string> host = {};
  std::optional<int> port = {};
  std::string database = {};
  std::string query = {};  // Without the "?".
};

// Parses `url` like sqlalchemy.engine.url.make_url(); nullopt if it is not
// a URL.
std::optional<ConnectionUrl> ParseConnectionUrl(std::string_view url);

// Returns `url` as SQLAlchemy writes it (Python: str(url) and
// url.render_as_string()); the password is "***" if `hide_password`.
std::string FormatConnectionUrl(const ConnectionUrl& url, bool hide_password);

// Returns `url` with a relative SQLite database path joined to the folder
// `workspace`; other URLs are returned as they are (Python:
// SQLServer.resolve_sqlite_relative_path()).
ConnectionUrl ResolveSqliteRelativePath(ConnectionUrl url,
                                        std::string_view workspace);

// A database of the server (Python: SQLServer). Copyable.
class Database {
 public:
  // The SQLite database file `path` (Python: SQLiteDatabase).
  static Database Sqlite(std::string path);

  // The PostgreSQL database `params` (Python: PostgreSQLServer).
  static Database Postgresql(PostgresqlParams params);

  // Returns the database of the SQLAlchemy URL `url` (Python:
  // SQLServer.from_connection_string()). A relative SQLite path is resolved
  // against the folder `workspace`. Fails if `url` is neither SQLite nor
  // PostgreSQL.
  static Result<Database> FromConnectionString(std::string_view url,
                                               std::string_view workspace);

  Database(const Database& other) = default;
  Database& operator=(const Database& other) = default;

  Dialect dialect() const {
    return std::holds_alternative<std::string>(location_)
               ? Dialect::kSqlite
               : Dialect::kPostgresql;
  }

  // Returns the SQLAlchemy URL of the database, with the password (Python:
  // get_connection_string()). Never log it.
  std::string ConnectionString() const;

  // Returns the URL with the password hidden, for messages.
  std::string SafeConnectionString() const;

  // Returns where the database is, for users: the SQLite file path, or
  // "<host>:<port>/<database>" (Python: get_db_location()).
  std::string Location() const;

  // Opens a new connection to the database.
  Result<std::unique_ptr<Connection>> Open() const;

 private:
  explicit Database(std::variant<std::string, PostgresqlParams> location)
      : location_(std::move(location)) {}

  // Returns the URL of the database.
  ConnectionUrl Url() const;

  // The path of the SQLite file, or the PostgreSQL connection.
  std::variant<std::string, PostgresqlParams> location_;
};

// A connection to a database that is opened when it is first needed and
// then kept. Each process keeps its own (architecture.md §11.5). Move-only;
// not thread-safe.
class LazyConnection {
 public:
  explicit LazyConnection(Database database) : database_(std::move(database)) {}

  LazyConnection(LazyConnection&& other) = default;
  LazyConnection& operator=(LazyConnection&& other) = default;

  const Database& database() const { return database_; }

  // Returns the connection, opened first if needed. The pointer is valid
  // until Close() or the destruction of this object.
  Result<Connection*> Get();

  // Runs Connection::Query() or db::QueryValue() on the connection, opened
  // first if needed.
  Result<QueryResult> Query(std::string_view sql,
                            const std::vector<Param>& params);
  Result<std::optional<std::string>> QueryValue(
      std::string_view sql, const std::vector<Param>& params);

  // Closes the connection if it is open.
  void Close() { connection_.reset(); }

 private:
  Database database_;
  std::unique_ptr<Connection> connection_;  // Null until Get().
};

}  // namespace cc::server::db

#endif  // CC_SERVER_DATABASE_DATABASE_H_
