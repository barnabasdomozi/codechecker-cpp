// Port of report-converter's twodim.py: two-dimensional data in the output
// formats of the commands (common-infrastructure.md §7).

#ifndef CC_UTIL_TWODIM_H_
#define CC_UTIL_TWODIM_H_

#include <string>
#include <string_view>
#include <vector>

namespace cc::util::twodim {

// Returns `rows` under the header `keys` in `format`, which is "rows",
// "table", "plaintext", "csv" or "json" (Python: twodim.to_str()). Tables
// are fitted to the terminal width: COLUMNS, else the terminal on stdout,
// else 80 columns.
//
// Widths are counted in code points. prettytable counts double-width and
// zero-width characters with wcwidth; that comes with the first table
// that can hold such text.
std::string ToString(std::string_view format,
                     const std::vector<std::string>& keys,
                     const std::vector<std::vector<std::string>>& rows);

}  // namespace cc::util::twodim

#endif  // CC_UTIL_TWODIM_H_
