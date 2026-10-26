#include "server/database/connection.h"

#include <libpq-fe.h>
#include <sqlite3.h>
#include <stdint.h>

#include <charconv>
#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "util/result.h"
#include "util/strings.h"

namespace cc::server::db {
namespace {

// Returns the error of a failed database call with `message`, without the
// trailing newline libpq adds.
Error DatabaseError(std::string_view message) {
  return Error{.message = std::string(util::RightStrip(message))};
}

// A SQLite connection.
class SqliteConnection final : public Connection {
 public:
  explicit SqliteConnection(sqlite3* db) : db_(db) {}
  ~SqliteConnection() override { sqlite3_close_v2(db_); }

  Dialect dialect() const override { return Dialect::kSqlite; }

  Result<void> Execute(std::string_view sql) override {
    char* message = nullptr;
    if (sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr,
                     &message) != SQLITE_OK) {
      Error error = DatabaseError(message ? message : sqlite3_errmsg(db_));
      sqlite3_free(message);
      return Result<void>(std::move(error));
    }
    return Result<void>();
  }

  Result<QueryResult> Query(std::string_view sql,
                            const std::vector<Param>& params) override;

 private:
  // Binds `params` to `statement`; returns whether it succeeded.
  bool Bind(sqlite3_stmt* statement, const std::vector<Param>& params);

  sqlite3* db_;
};

bool SqliteConnection::Bind(sqlite3_stmt* statement,
                            const std::vector<Param>& params) {
  for (size_t i = 0; i < params.size(); ++i) {
    const int index = static_cast<int>(i) + 1;
    const Param& param = params[i];
    int result = SQLITE_OK;
    if (std::holds_alternative<std::nullptr_t>(param)) {
      result = sqlite3_bind_null(statement, index);
    } else if (const bool* flag = std::get_if<bool>(&param)) {
      result = sqlite3_bind_int(statement, index, *flag ? 1 : 0);
    } else if (const int64_t* number = std::get_if<int64_t>(&param)) {
      result = sqlite3_bind_int64(statement, index, *number);
    } else if (const std::string* text = std::get_if<std::string>(&param)) {
      result = sqlite3_bind_text64(statement, index, text->data(),
                                   text->size(), SQLITE_TRANSIENT, SQLITE_UTF8);
    } else {
      const std::string& data = std::get<Bytes>(param).data;
      result = sqlite3_bind_blob64(statement, index, data.data(), data.size(),
                                   SQLITE_TRANSIENT);
    }
    if (result != SQLITE_OK) return false;
  }
  return true;
}

Result<QueryResult> SqliteConnection::Query(std::string_view sql,
                                            const std::vector<Param>& params) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()),
                         &statement, nullptr) != SQLITE_OK) {
    return Result<QueryResult>(DatabaseError(sqlite3_errmsg(db_)));
  }
  const std::unique_ptr<sqlite3_stmt, int (*)(sqlite3_stmt*)> owner(
      statement, &sqlite3_finalize);
  if (!Bind(statement, params)) {
    return Result<QueryResult>(DatabaseError(sqlite3_errmsg(db_)));
  }
  QueryResult result;
  const int columns = sqlite3_column_count(statement);
  int step = SQLITE_OK;
  while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
    std::vector<std::optional<std::string>> cells;
    for (int column = 0; column < columns; ++column) {
      const int type = sqlite3_column_type(statement, column);
      if (type == SQLITE_NULL) {
        cells.emplace_back();
      } else if (type == SQLITE_BLOB) {
        const void* data = sqlite3_column_blob(statement, column);
        const int size = sqlite3_column_bytes(statement, column);
        cells.emplace_back(std::string(static_cast<const char*>(data), size));
      } else {
        const unsigned char* text = sqlite3_column_text(statement, column);
        const int size = sqlite3_column_bytes(statement, column);
        cells.emplace_back(
            std::string(reinterpret_cast<const char*>(text), size));
      }
    }
    result.rows.emplace_back(Dialect::kSqlite, std::move(cells));
  }
  if (step != SQLITE_DONE) {
    return Result<QueryResult>(DatabaseError(sqlite3_errmsg(db_)));
  }
  result.changed_rows = sqlite3_changes64(db_);
  return Result<QueryResult>(std::move(result));
}

// Frees libpq results.
struct PgResultDeleter {
  void operator()(PGresult* result) const { PQclear(result); }
};
using PgResult = std::unique_ptr<PGresult, PgResultDeleter>;

// A PostgreSQL connection.
class PostgresqlConnection final : public Connection {
 public:
  explicit PostgresqlConnection(PGconn* connection) : connection_(connection) {}
  ~PostgresqlConnection() override { PQfinish(connection_); }

  Dialect dialect() const override { return Dialect::kPostgresql; }

  Result<void> Execute(std::string_view sql) override {
    const PgResult result(PQexec(connection_, std::string(sql).c_str()));
    const ExecStatusType status = PQresultStatus(result.get());
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
      return Result<void>(DatabaseError(PQresultErrorMessage(result.get())));
    }
    return Result<void>();
  }

  Result<QueryResult> Query(std::string_view sql,
                            const std::vector<Param>& params) override;

 private:
  PGconn* connection_;
};

// Returns `sql` with its "?" placeholders numbered as PostgreSQL wants them:
// "$1", "$2", ... Question marks in quoted strings and names stay.
std::string NumberPlaceholders(std::string_view sql) {
  std::string numbered;
  int count = 0;
  char quote = '\0';
  for (const char c : sql) {
    if (quote != '\0') {
      if (c == quote) quote = '\0';
      numbered += c;
    } else if (c == '\'' || c == '"') {
      quote = c;
      numbered += c;
    } else if (c == '?') {
      numbered += std::format("${}", ++count);
    } else {
      numbered += c;
    }
  }
  return numbered;
}

Result<QueryResult> PostgresqlConnection::Query(
    std::string_view sql, const std::vector<Param>& params) {
  // Values are sent as text, so the server takes their types from the
  // statement; binary data is sent as bytea.
  constexpr Oid kByteaOid = 17;
  std::vector<std::string> texts(params.size());
  std::vector<const char*> values(params.size(), nullptr);
  std::vector<int> lengths(params.size(), 0);
  std::vector<int> formats(params.size(), 0);
  std::vector<Oid> types(params.size(), 0);
  for (size_t i = 0; i < params.size(); ++i) {
    const Param& param = params[i];
    if (std::holds_alternative<std::nullptr_t>(param)) continue;
    if (const bool* flag = std::get_if<bool>(&param)) {
      texts[i] = *flag ? "true" : "false";
    } else if (const int64_t* number = std::get_if<int64_t>(&param)) {
      texts[i] = std::to_string(*number);
    } else if (const std::string* text = std::get_if<std::string>(&param)) {
      texts[i] = *text;
    } else {
      texts[i] = std::get<Bytes>(param).data;
      formats[i] = 1;
      types[i] = kByteaOid;
    }
    values[i] = texts[i].data();
    lengths[i] = static_cast<int>(texts[i].size());
  }
  const PgResult result(PQexecParams(
      connection_, NumberPlaceholders(sql).c_str(),
      static_cast<int>(params.size()), types.data(), values.data(),
      lengths.data(), formats.data(), /*resultFormat=*/0));
  const ExecStatusType status = PQresultStatus(result.get());
  if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
    return Result<QueryResult>(
        DatabaseError(PQresultErrorMessage(result.get())));
  }
  QueryResult query_result;
  const int rows = PQntuples(result.get());
  const int columns = PQnfields(result.get());
  for (int row = 0; row < rows; ++row) {
    std::vector<std::optional<std::string>> cells;
    for (int column = 0; column < columns; ++column) {
      if (PQgetisnull(result.get(), row, column)) {
        cells.emplace_back();
      } else {
        cells.emplace_back(std::string(
            PQgetvalue(result.get(), row, column),
            static_cast<size_t>(PQgetlength(result.get(), row, column))));
      }
    }
    query_result.rows.emplace_back(Dialect::kPostgresql, std::move(cells));
  }
  const std::string_view changed = PQcmdTuples(result.get());
  std::from_chars(changed.data(), changed.data() + changed.size(),
                  query_result.changed_rows);
  return Result<QueryResult>(std::move(query_result));
}

}  // namespace

std::string Row::Text(int column) const {
  return cells_[column].value_or(std::string());
}

std::optional<std::string> Row::OptionalText(int column) const {
  return cells_[column];
}

int64_t Row::Int(int column) const {
  return OptionalInt(column).value_or(0);
}

std::optional<int64_t> Row::OptionalInt(int column) const {
  if (!cells_[column]) return std::nullopt;
  const std::string& text = *cells_[column];
  int64_t value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

bool Row::Bool(int column) const {
  if (!cells_[column]) return false;
  const std::string& text = *cells_[column];
  return text == "1" || text == "t" || text == "true";
}

std::string Row::Blob(int column) const {
  if (!cells_[column]) return {};
  const std::string& text = *cells_[column];
  if (dialect_ == Dialect::kSqlite) return text;
  // PostgreSQL sends bytea in its text form, which libpq decodes.
  size_t size = 0;
  unsigned char* data = PQunescapeBytea(
      reinterpret_cast<const unsigned char*>(text.c_str()), &size);
  if (data == nullptr) return {};
  std::string blob(reinterpret_cast<const char*>(data), size);
  PQfreemem(data);
  return blob;
}

Result<std::unique_ptr<Connection>> OpenSqlite(const std::string& path) {
  using Opened = Result<std::unique_ptr<Connection>>;
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    Error error = DatabaseError(db ? sqlite3_errmsg(db) : "out of memory");
    sqlite3_close_v2(db);
    return Opened(std::move(error));
  }
  auto connection = std::make_unique<SqliteConnection>(db);
  sqlite3_busy_timeout(db, 600 * 1000);
  Result<void> pragma = connection->Execute("PRAGMA foreign_keys=ON");
  if (!pragma.ok()) return Opened(pragma.error());
  return Opened(std::move(connection));
}

Result<std::unique_ptr<Connection>> OpenPostgresql(
    const PostgresqlParams& params) {
  using Opened = Result<std::unique_ptr<Connection>>;
  const std::string port = std::to_string(params.port);
  std::vector<const char*> keys = {"host",           "port",
                                   "user",           "dbname",
                                   "connect_timeout", "client_encoding"};
  std::vector<const char*> values = {params.host.c_str(),
                                     port.c_str(),
                                     params.user.c_str(),
                                     params.database.c_str(),
                                     "60",
                                     "UTF8"};
  if (params.password) {
    keys.push_back("password");
    values.push_back(params.password->c_str());
  }
  keys.push_back(nullptr);
  values.push_back(nullptr);
  PGconn* connection =
      PQconnectdbParams(keys.data(), values.data(), /*expand_dbname=*/0);
  if (PQstatus(connection) != CONNECTION_OK) {
    Error error = DatabaseError(PQerrorMessage(connection));
    PQfinish(connection);
    return Opened(std::move(error));
  }
  return Opened(std::make_unique<PostgresqlConnection>(connection));
}

Result<std::optional<std::string>> QueryValue(
    Connection& connection, std::string_view sql,
    const std::vector<Param>& params) {
  using Value = Result<std::optional<std::string>>;
  Result<QueryResult> result = connection.Query(sql, params);
  if (!result.ok()) return Value(result.error());
  if (result.value().rows.empty()) return Value(std::nullopt);
  return Value(result.value().rows.front().OptionalText(0));
}

Result<void> RunInTransaction(Connection& connection,
                              const std::function<Result<void>()>& body) {
  Result<void> begun = connection.Execute("BEGIN");
  if (!begun.ok()) return begun;
  Result<void> result = body();
  if (result.ok()) result = connection.Execute("COMMIT");
  if (!result.ok()) (void)connection.Execute("ROLLBACK");
  return result;
}

}  // namespace cc::server::db
