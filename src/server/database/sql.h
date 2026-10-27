// The query builder of the server (architecture.md §11.5): SQL statements
// put together from parts, the way the Python server builds them with
// SQLAlchemy. A part keeps its text with "?" placeholders and the values of
// the placeholders in the same order, so parts can be nested in any order.
//
//   db::Select select = {.columns = db::Sql("reports.id"),
//                        .from = db::Sql("reports")};
//   select.where.push_back(db::In("reports.run_id", run_ids));
//   select.where.push_back(db::ILike(db::Sql("reports.bug_id"), "abc%"));
//   const db::Sql sql = db::Build(select);
//   connection.Query(sql.text(), sql.params());
//
// The SQL is the same for SQLite and PostgreSQL.

#ifndef CC_SERVER_DATABASE_SQL_H_
#define CC_SERVER_DATABASE_SQL_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "server/database/connection.h"

namespace cc::server::db {

// A part of a SQL statement and the values of its placeholders. Copyable.
class Sql {
 public:
  Sql() = default;
  explicit Sql(std::string_view text) : text_(text) {}

  Sql(const Sql& other) = default;
  Sql& operator=(const Sql& other) = default;
  Sql(Sql&& other) = default;
  Sql& operator=(Sql&& other) = default;

  const std::string& text() const { return text_; }
  const std::vector<Param>& params() const { return params_; }
  bool empty() const { return text_.empty(); }

  // Appends `text`, which has no placeholders.
  Sql& Add(std::string_view text);
  // Appends `other` with its placeholder values.
  Sql& Add(const Sql& other);
  // Appends a "?" placeholder for `value`.
  Sql& Bind(Param value);

 private:
  std::string text_;
  std::vector<Param> params_;
};

// Returns `parts` joined by `separator`.
Sql Join(const std::vector<Sql>& parts, std::string_view separator);

// Returns "(<p1> AND <p2> ...)"; "true" if `parts` is empty (Python:
// and_(), true()).
Sql And(const std::vector<Sql>& parts);

// Returns "(<p1> OR <p2> ...)"; "false" if `parts` is empty (Python: or_()).
Sql Or(const std::vector<Sql>& parts);

// Returns "<column> IN (...)" with a placeholder for each value; "false" if
// `values` is empty (Python: column.in_(values)).
Sql In(std::string_view column, const std::vector<std::string>& values);

// Returns "<column> IN (...)" with the numbers written in the SQL.
Sql In(std::string_view column, const std::vector<int64_t>& values);

// Returns "<column> IN (<query>)".
Sql In(std::string_view column, const Sql& query);

// Returns "<column> NOT IN (...)"; "true" if `values` is empty (Python:
// column.notin_(values)).
Sql NotIn(std::string_view column, const std::vector<std::string>& values);

// Returns the case-insensitive LIKE of `expression` and `pattern`: "lower(
// <expression>) LIKE lower(?)", as SQLAlchemy writes column.ilike(pattern)
// for SQLite.
Sql ILike(const Sql& expression, std::string pattern);

// Returns ILike() with '\' as the escape character of `pattern` (Python:
// column.ilike(pattern, escape='\\')).
Sql ILikeEscaped(const Sql& expression, std::string pattern);

// The clauses of a SELECT statement. They can be filled in any order, as
// with SQLAlchemy's Query; Build() writes them in SQL's order.
struct Select {
  Sql columns = {};
  bool distinct = false;
  Sql from = {};                   // The tables and their joins.
  std::vector<Sql> where = {};     // Joined by AND.
  std::vector<Sql> group_by = {};  // Joined by ", ".
  std::vector<Sql> having = {};    // Joined by AND.
  std::vector<Sql> order_by = {};  // Joined by ", ".
  int64_t limit = 0;               // 0: no LIMIT.
  int64_t offset = 0;  // Only written with a LIMIT; 0: no OFFSET.
};

// Returns the statement of `select`.
Sql Build(const Select& select);

// Returns "SELECT count(*) FROM (<query>) AS counted" (Python:
// query.count()).
Sql Count(const Sql& query);

}  // namespace cc::server::db

#endif  // CC_SERVER_DATABASE_SQL_H_
