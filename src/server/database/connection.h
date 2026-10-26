// Connections to the server's databases (architecture.md §11.5): one
// interface with an implementation for SQLite (libsqlite3) and one for
// PostgreSQL (libpq). SQL is written with "?" placeholders for both.
//
//   Result<std::unique_ptr<db::Connection>> connection = database.Open();
//   Result<db::QueryResult> rows = connection.value()->Query(
//       "SELECT id, endpoint FROM products WHERE endpoint = ?", {endpoint});
//   for (const db::Row& row : rows.value().rows) Use(row.Int(0));

#ifndef CC_SERVER_DATABASE_CONNECTION_H_
#define CC_SERVER_DATABASE_CONNECTION_H_

#include <stdint.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "util/result.h"

namespace cc::server::db {

// The SQL dialect of a database.
enum class Dialect { kSqlite, kPostgresql };

// Binary data for a BLOB (SQLite) or bytea (PostgreSQL) parameter.
struct Bytes {
  std::string data;
};

// A value bound to a "?" placeholder: NULL, a boolean, an integer, a text or
// binary data.
using Param = std::variant<std::nullptr_t, bool, int64_t, std::string, Bytes>;

// One row of a query result. The values are kept in the database's text
// form and converted by the accessors, which take the 0-based column index.
class Row {
 public:
  Row(Dialect dialect, std::vector<std::optional<std::string>> cells)
      : dialect_(dialect), cells_(std::move(cells)) {}

  Row(const Row& other) = default;
  Row& operator=(const Row& other) = default;

  bool IsNull(int column) const { return !cells_[column].has_value(); }
  // Returns the text of the column; "" for NULL.
  std::string Text(int column) const;
  std::optional<std::string> OptionalText(int column) const;
  // Returns the integer in the column; 0 for NULL.
  int64_t Int(int column) const;
  std::optional<int64_t> OptionalInt(int column) const;
  // Returns the boolean in the column; false for NULL.
  bool Bool(int column) const;
  // Returns the binary data in the column; "" for NULL.
  std::string Blob(int column) const;

 private:
  Dialect dialect_;
  std::vector<std::optional<std::string>> cells_;
};

// The result of a statement.
struct QueryResult {
  std::vector<Row> rows;
  // The number of rows an INSERT, UPDATE or DELETE changed.
  int64_t changed_rows = 0;
};

// An open connection to a database. A connection is used by one thread at a
// time.
class Connection {
 public:
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  virtual ~Connection() = default;

  virtual Dialect dialect() const = 0;

  // Runs `sql`, which may hold several statements without parameters. The
  // error message is the database's.
  virtual Result<void> Execute(std::string_view sql) = 0;

  // Runs the single statement `sql` with its "?" placeholders bound to
  // `params`, in order, and returns its rows. The error message is the
  // database's.
  virtual Result<QueryResult> Query(std::string_view sql,
                                    const std::vector<Param>& params) = 0;

 protected:
  Connection() = default;
};

// The settings of a PostgreSQL connection.
struct PostgresqlParams {
  std::string host = {};
  int port = 5432;
  std::string user = {};
  // nullopt: libpq looks it up in the password file (PGPASSFILE or
  // ~/.pgpass).
  std::optional<std::string> password = {};
  std::string database = {};
};

// Opens the SQLite database file `path`, creating it if it is missing, with
// foreign keys turned on and a busy timeout of 600 seconds, as the Python
// server does.
Result<std::unique_ptr<Connection>> OpenSqlite(const std::string& path);

// Connects to the PostgreSQL database `params`, with a connect timeout of
// 60 seconds and UTF-8 client encoding, as the Python server does.
Result<std::unique_ptr<Connection>> OpenPostgresql(
    const PostgresqlParams& params);

// Runs `sql` with `params` and returns the first column of its first row;
// nullopt if there is no row or the value is NULL.
Result<std::optional<std::string>> QueryValue(
    Connection& connection, std::string_view sql,
    const std::vector<Param>& params);

// Runs `body` on `connection` in a transaction: BEGIN, `body`, COMMIT. If
// `body` or COMMIT fails, the transaction is rolled back and that error is
// returned; if BEGIN fails, its error is returned and `body` does not run.
Result<void> RunInTransaction(Connection& connection,
                              const std::function<Result<void>()>& body);

}  // namespace cc::server::db

#endif  // CC_SERVER_DATABASE_CONNECTION_H_
