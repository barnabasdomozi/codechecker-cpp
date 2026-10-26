#include "common/cli/logo.h"

#include <stddef.h>

#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "platform/subprocess.h"
#include "platform/terminal.h"
#include "util/strings.h"

namespace cc::common::cli {
namespace {

// The logo, "codechecker-cpp" with a few sparks around it, drawn with these
// characters: '#' is a full block, '^' its upper half and '_' its lower half;
// '*', '+' and '.' are sparks.
constexpr std::string_view kArt = R"(
 +              #           #              #              .       *      .
    ___  __   __#  __   ___ #__   __   ___ #  _  __  _ __      ___ ___  ___  +
.  #    #  # #  # #__# #    #  # #__# #    #_^  #__# #^   ___ #    #  # #  #
   ^___ ^__^ ^__# ^___ ^___ #  # ^___ ^___ # ^_ ^___ #        ^___ #__^ #__^  .
   .      *                                     +                  #    #   .
)";

// The width of kArt (its widest row).
constexpr size_t kWidth = 79;

// xterm 256-color codes, from left to right: the letters go from blue to
// purple, and the sparks are lighter tints of the same colors.
constexpr int kLetterColors[] = {39, 33, 69, 63, 99, 135};
constexpr int kSparkColors[] = {117, 111, 147, 141, 183};

bool IsSpark(char mask) { return mask == '*' || mask == '+' || mask == '.'; }

// Returns the character that `mask` in kArt stands for.
std::string_view Glyph(char mask) {
  switch (mask) {
    case '#':
      return "█";  // "█"
    case '^':
      return "▀";  // "▀"
    case '_':
      return "▄";  // "▄"
    case '*':
      return "✦";  // "✦"
    case '+':
      return "✧";  // "✧"
    case '.':
      return "∙";  // "∙"
    default:
      return " ";
  }
}

// Returns the color of `mask` at `column` of kArt.
int Color(char mask, size_t column) {
  if (IsSpark(mask)) {
    return kSparkColors[column * std::size(kSparkColors) / kWidth];
  }
  return kLetterColors[column * std::size(kLetterColors) / kWidth];
}

}  // namespace

std::string TerminalLogo() {
  const std::optional<int> columns = platform::StdoutTerminalColumns();
  if (!columns || *columns < static_cast<int>(kWidth)) return "";
  const bool color = platform::GetEnv("NO_COLOR").value_or("").empty();
  // Returns the escape sequence that switches to bold text in `code`.
  const auto paint = [color](int code) {
    return color ? std::format("\x1b[1;38;5;{}m", code) : std::string();
  };
  const std::string reset = color ? "\x1b[0m" : "";

  std::string logo;
  for (const std::string_view row : util::StrSplitLines(kArt.substr(1))) {
    std::optional<int> current;
    for (size_t column = 0; column < row.size(); ++column) {
      const char mask = row[column];
      if (mask != ' ') {
        const int color = Color(mask, column);
        if (color != current) {
          current = color;
          logo += paint(color);
        }
      }
      logo += Glyph(mask);
    }
    logo += reset + "\n";
  }
  logo += "\n";
  return logo;
}

}  // namespace cc::common::cli
