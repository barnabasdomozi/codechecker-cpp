#include "util/twodim.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/json.h"
#include "util/strings.h"
#include "util/terminal.h"
#include "util/textwrap.h"
#include "util/unicode.h"

namespace cc::util::twodim {
namespace {

using Row = std::vector<std::string>;

// The narrowest a column gets when a table is fitted to the terminal.
constexpr int kMinColumnWidth = 4;

int Length(std::string_view text) {
  return static_cast<int>(CodePointCount(text));
}

std::vector<std::string_view> SplitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  for (size_t end; (end = text.find('\n', start)) != std::string_view::npos;
       start = end + 1) {
    lines.push_back(text.substr(start, end - start));
  }
  lines.push_back(text.substr(start));
  return lines;
}

// Returns the width of the widest line of `text` (prettytable: _get_size()).
int BlockWidth(std::string_view text) {
  int width = 0;
  for (const std::string_view line : SplitLines(text)) {
    width = std::max(width, Length(line));
  }
  return width;
}

std::string Repeat(std::string_view text, int count) {
  std::string result;
  for (int i = 0; i < count; ++i) result += text;
  return result;
}

// Python: twodim.to_rows().
std::string ToRows(const std::vector<Row>& rows) {
  std::vector<int> widths;
  for (const Row& row : rows) {
    widths.resize(std::max(widths.size(), row.size()), 0);
    for (size_t i = 0; i < row.size(); ++i) {
      widths[i] = std::max(widths[i], Length(row[i]));
    }
  }
  std::vector<std::string> lines;
  for (const Row& row : rows) {
    std::vector<std::string> cells;
    for (size_t i = 0; i < row.size(); ++i) {
      // The first and the last column are not padded.
      const bool pad = i != 0 && i != widths.size() - 1 && widths[i] != 0;
      cells.push_back(pad ? LeftJustify(row[i], widths[i]) : row[i]);
    }
    lines.push_back(" " + Join(cells, " "));
  }
  return Join(lines, "\n");
}

// Python: twodim.to_csv(). Cells are not quoted.
std::string ToCsv(const std::vector<Row>& rows) {
  std::vector<std::string> lines;
  for (const Row& row : rows) lines.push_back(Join(row, ","));
  return Join(lines, "\n");
}

// Python: json.dumps(twodim.to_dictlist(keys, rows)).
std::string ToJson(const Row& keys, const std::vector<Row>& rows) {
  const MutableJsonDocument document = NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* list = yyjson_mut_arr(doc);
  for (const Row& row : rows) {
    yyjson_mut_val* object = yyjson_mut_arr_add_obj(doc, list);
    for (size_t i = 0; i < std::min(keys.size(), row.size()); ++i) {
      yyjson_mut_obj_add(
          object, yyjson_mut_strncpy(doc, keys[i].data(), keys[i].size()),
          yyjson_mut_strncpy(doc, row[i].data(), row[i].size()));
    }
  }
  return DumpJson(list);
}

// The column widths for a table that does not fit `terminal_width`: short
// columns keep their natural width, and the wide ones share the rest in
// proportion to their natural width (Python: twodim._fit_table_to_width()).
std::vector<int> FitColumns(const std::vector<int>& natural_widths,
                            int terminal_width) {
  const int columns = static_cast<int>(natural_widths.size());
  const int overhead = columns * 3 + 1;
  std::vector<int> assigned(columns, 0);
  std::vector<int> unassigned;
  for (int i = 0; i < columns; ++i) unassigned.push_back(i);
  int remaining =
      std::max(terminal_width - overhead, columns * kMinColumnWidth);
  // Each round locks the columns that fit an equal share of what remains.
  for (int round = 0; round <= columns && !unassigned.empty(); ++round) {
    const int equal_share = remaining / static_cast<int>(unassigned.size());
    std::vector<int> still_unassigned;
    for (const int i : unassigned) {
      if (natural_widths[i] <= equal_share) {
        assigned[i] = natural_widths[i];
        remaining -= natural_widths[i];
      } else {
        still_unassigned.push_back(i);
      }
    }
    if (still_unassigned.size() == unassigned.size()) break;
    unassigned = still_unassigned;
  }
  int natural_sum = 0;
  for (const int i : unassigned) natural_sum += natural_widths[i];
  for (const int i : unassigned) {
    // Python computes the share in floating point and truncates it.
    const int share =
        natural_sum > 0
            ? static_cast<int>(static_cast<double>(natural_widths[i]) /
                               natural_sum * remaining)
            : remaining / static_cast<int>(unassigned.size());
    assigned[i] = std::max(kMinColumnWidth, share);
  }
  return assigned;
}

// Returns the column widths prettytable prints with: the widest cell of
// each column and of the header if it is shown, cut to `max_widths` for
// data cells but not for the header, then shrunk in proportion if the table
// is wider than `max_table_width` (prettytable: _compute_widths()). Without
// `max_widths`, nothing is cut.
std::vector<int> ColumnWidths(const Row& header, bool show_header,
                              const std::vector<Row>& rows,
                              const std::optional<std::vector<int>>& max_widths,
                              std::optional<int> max_table_width) {
  std::vector<int> widths(header.size(), 0);
  for (size_t i = 0; show_header && i < header.size(); ++i) {
    widths[i] = BlockWidth(header[i]);
  }
  for (const Row& row : rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      const int width = BlockWidth(row[i]);
      widths[i] = std::max(
          widths[i], max_widths ? std::min(width, (*max_widths)[i]) : width);
    }
  }
  const int columns = static_cast<int>(widths.size());
  int table_width = 1;
  for (const int width : widths) table_width += width + 3;
  if (max_table_width && table_width > *max_table_width) {
    const int markup = columns * 3 - 1;
    const double scale = static_cast<double>(*max_table_width - markup) /
                         (table_width - markup);
    for (int& width : widths) {
      width = std::max(1, static_cast<int>(width * scale));
    }
  }
  return widths;
}

// Returns a horizontal rule of the table, from the box-drawing characters
// for its left end, the column junctions and its right end.
std::string Rule(const std::vector<int>& widths, std::string_view left,
                 std::string_view junction, std::string_view right) {
  std::string rule(left);
  for (size_t i = 0; i < widths.size(); ++i) {
    if (i > 0) rule += junction;
    rule += Repeat("─", widths[i] + 2);
  }
  rule += right;
  return rule;
}

// Returns the lines of one table row; cells wider than their column are
// wrapped (prettytable: _stringify_row()).
std::vector<std::string> RowLines(const Row& row,
                                  const std::vector<int>& widths) {
  std::vector<std::vector<std::string>> cells;
  size_t height = 0;
  for (size_t i = 0; i < row.size(); ++i) {
    std::vector<std::string> lines;
    for (const std::string_view line : SplitLines(row[i])) {
      if (Length(line) <= widths[i]) {
        lines.emplace_back(line);
        continue;
      }
      const std::vector<std::string> wrapped = Wrap(line, widths[i]);
      if (wrapped.empty()) lines.emplace_back();
      lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    height = std::max(height, lines.size());
    cells.push_back(std::move(lines));
  }
  std::vector<std::string> result;
  for (size_t y = 0; y < height; ++y) {
    std::string line = "│";
    for (size_t i = 0; i < cells.size(); ++i) {
      const std::string_view text =
          y < cells[i].size() ? std::string_view(cells[i][y]) : "";
      line += " " + LeftJustify(text, widths[i]) + " │";
    }
    result.push_back(std::move(line));
  }
  return result;
}

// Returns the table prettytable prints in the SINGLE_BORDER style, left
// aligned (prettytable: get_string()); `style.rule_between_rows` is
// prettytable's HRuleStyle.ALL instead of FRAME. `header` gives the number
// of columns, also when it is not shown.
std::string RenderTable(const Row& header, const std::vector<Row>& rows,
                        TableStyle style,
                        const std::optional<std::vector<int>>& max_widths,
                        std::optional<int> max_table_width) {
  const std::vector<int> widths = ColumnWidths(
      header, style.show_header, rows, max_widths, max_table_width);
  const std::string middle_rule = Rule(widths, "├", "┼", "┤");
  std::vector<std::string> lines = {Rule(widths, "┌", "┬", "┐")};
  if (style.show_header) {
    std::string header_line = "│";
    for (size_t i = 0; i < header.size(); ++i) {
      const std::string_view name(
          header[i].data(), CodePointPrefixSize(header[i], widths[i]));
      header_line += " " + LeftJustify(name, widths[i]) + " │";
    }
    lines.push_back(header_line);
    lines.push_back(middle_rule);
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i > 0 && style.rule_between_rows) lines.push_back(middle_rule);
    for (std::string& line : RowLines(rows[i], widths)) {
      lines.push_back(std::move(line));
    }
  }
  lines.push_back(Rule(widths, "└", "┴", "┘"));
  return Join(lines, "\n");
}

}  // namespace

std::string ToTable(const std::vector<Row>& lines, TableStyle style) {
  if (lines.empty()) return "";
  const int terminal_width = ColumnsVariable().value_or(TerminalColumns());
  const Row& header = lines.front();
  const size_t first_row = style.show_header ? 1 : 0;
  // prettytable expands tabs in the data cells.
  std::vector<Row> rows;
  for (size_t i = first_row; i < lines.size(); ++i) {
    Row row;
    for (const std::string& cell : lines[i]) row.push_back(ExpandTabs(cell));
    rows.push_back(std::move(row));
  }
  // The natural widths, measured before tabs are expanded, as in twodim.
  std::vector<int> natural_widths(header.size(), 0);
  for (size_t j = 0; style.show_header && j < header.size(); ++j) {
    natural_widths[j] = Length(header[j]);
  }
  for (size_t i = first_row; i < lines.size(); ++i) {
    for (size_t j = 0; j < lines[i].size(); ++j) {
      natural_widths[j] = std::max(natural_widths[j], Length(lines[i][j]));
    }
  }
  int total = static_cast<int>(natural_widths.size()) * 3 + 1;
  for (const int width : natural_widths) total += width;
  if (total <= terminal_width) {
    return RenderTable(header, rows, style, std::nullopt, std::nullopt);
  }
  return RenderTable(header, rows, style,
                     FitColumns(natural_widths, terminal_width),
                     terminal_width);
}

std::string ToString(std::string_view format, const Row& keys,
                     const std::vector<Row>& rows) {
  std::vector<Row> all_rows;
  if (!keys.empty()) all_rows.push_back(keys);
  all_rows.insert(all_rows.end(), rows.begin(), rows.end());
  if (format == "rows") return ToRows(rows);
  if (format == "table" || format == "plaintext") {
    return ToTable(all_rows, TableStyle());
  }
  if (format == "csv") return ToCsv(all_rows);
  if (format == "json") return ToJson(keys, rows);
  // Python raises ValueError("Unsupported format"); callers only pass the
  // formats of their --output choices.
  std::terminate();
}

}  // namespace cc::util::twodim
