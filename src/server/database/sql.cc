#include "server/database/sql.h"

#include <stddef.h>
#include <stdint.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "server/database/connection.h"

namespace cc::server::db {
namespace {

// Returns `parts` joined by `separator` in parentheses; `empty` if there are
// no parts.
Sql Combine(const std::vector<Sql>& parts, std::string_view separator,
            std::string_view empty) {
  if (parts.empty()) return Sql(empty);
  Sql result("(");
  result.Add(Join(parts, separator)).Add(")");
  return result;
}

// Returns "?, ?, ..." with a placeholder for each of `values`.
Sql Placeholders(const std::vector<std::string>& values) {
  Sql result;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) result.Add(", ");
    result.Bind(values[i]);
  }
  return result;
}

}  // namespace

Sql& Sql::Add(std::string_view text) {
  text_ += text;
  return *this;
}

Sql& Sql::Add(const Sql& other) {
  text_ += other.text_;
  params_.insert(params_.end(), other.params_.begin(), other.params_.end());
  return *this;
}

Sql& Sql::Bind(Param value) {
  text_ += '?';
  params_.push_back(std::move(value));
  return *this;
}

Sql Join(const std::vector<Sql>& parts, std::string_view separator) {
  Sql result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) result.Add(separator);
    result.Add(parts[i]);
  }
  return result;
}

Sql And(const std::vector<Sql>& parts) {
  return Combine(parts, " AND ", "true");
}

Sql Or(const std::vector<Sql>& parts) {
  return Combine(parts, " OR ", "false");
}

Sql In(std::string_view column, const std::vector<std::string>& values) {
  if (values.empty()) return Sql("false");
  Sql result(column);
  return result.Add(" IN (").Add(Placeholders(values)).Add(")");
}

Sql In(std::string_view column, const std::vector<int64_t>& values) {
  if (values.empty()) return Sql("false");
  std::string list;
  for (const int64_t value : values) {
    if (!list.empty()) list += ", ";
    list += std::to_string(value);
  }
  return Sql(std::format("{} IN ({})", column, list));
}

Sql In(std::string_view column, const Sql& query) {
  Sql result(column);
  return result.Add(" IN (").Add(query).Add(")");
}

Sql NotIn(std::string_view column, const std::vector<std::string>& values) {
  if (values.empty()) return Sql("true");
  Sql result(column);
  return result.Add(" NOT IN (").Add(Placeholders(values)).Add(")");
}

Sql ILike(const Sql& expression, std::string pattern) {
  Sql result("lower(");
  result.Add(expression).Add(") LIKE lower(").Bind(std::move(pattern));
  return result.Add(")");
}

Sql ILikeEscaped(const Sql& expression, std::string pattern) {
  return ILike(expression, std::move(pattern)).Add(" ESCAPE '\\'");
}

Sql Build(const Select& select) {
  Sql sql(select.distinct ? "SELECT DISTINCT " : "SELECT ");
  sql.Add(select.columns).Add(" FROM ").Add(select.from);
  if (!select.where.empty()) {
    sql.Add(" WHERE ").Add(Join(select.where, " AND "));
  }
  if (!select.group_by.empty()) {
    sql.Add(" GROUP BY ").Add(Join(select.group_by, ", "));
  }
  if (!select.having.empty()) {
    sql.Add(" HAVING ").Add(Join(select.having, " AND "));
  }
  if (!select.order_by.empty()) {
    sql.Add(" ORDER BY ").Add(Join(select.order_by, ", "));
  }
  if (select.limit > 0) {
    sql.Add(std::format(" LIMIT {}", select.limit));
    if (select.offset > 0) sql.Add(std::format(" OFFSET {}", select.offset));
  }
  return sql;
}

Sql Count(const Sql& query) {
  Sql sql("SELECT count(*) FROM (");
  return sql.Add(query).Add(") AS counted");
}

}  // namespace cc::server::db
