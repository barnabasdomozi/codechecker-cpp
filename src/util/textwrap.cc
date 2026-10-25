#include "util/textwrap.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/strings.h"
#include "util/unicode.h"

namespace cc::util {
namespace {

int Length(std::string_view text) {
  return static_cast<int>(CodePointCount(text));
}

bool IsBlank(std::string_view chunk) {
  return chunk.find_first_not_of(' ') == std::string_view::npos;
}

// Expands tabs and turns the other whitespace characters of textwrap (ASCII
// only) into spaces (TextWrapper._munge_whitespace()).
std::string MungeWhitespace(std::string_view text) {
  std::string result = ExpandTabs(text);
  for (char& c : result) {
    if (c == '\n' || c == '\v' || c == '\f' || c == '\r') c = ' ';
  }
  return result;
}

// The character classes of textwrap's wordsep_re, on code points; -1 stands
// for "outside the text". Python's \w also covers the letters and digits of
// other scripts; here every non-ASCII character counts as a letter.
bool IsWordChar(int64_t c) {
  return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

bool IsLetter(int64_t c) { return IsWordChar(c) && !(c >= '0' && c <= '9'); }

bool IsWordPunct(int64_t c) {
  return IsWordChar(c) || (c > 0 && std::string_view("!\"'&.,?").find(
                                        static_cast<char>(c)) !=
                                        std::string_view::npos);
}

// Splits whitespace-munged `text` into chunks: runs of spaces, words, and
// the parts of hyphenated words (TextWrapper._split() with wordsep_re).
std::vector<std::string_view> SplitChunks(std::string_view text) {
  std::vector<int64_t> chars;
  std::vector<size_t> offsets;
  for (size_t pos = 0; pos < text.size();) {
    offsets.push_back(pos);
    chars.push_back(NextCodePoint(text, &pos).value_or(0xFFFD));
  }
  offsets.push_back(text.size());
  const int64_t size = static_cast<int64_t>(chars.size());
  const auto at = [&](int64_t i) { return i >= 0 && i < size ? chars[i] : -1; };
  // Whether two or more dashes and a word character start at `i`.
  const auto dashes_then_word = [&](int64_t i) {
    int64_t end = i;
    while (at(end) == '-') ++end;
    return end - i >= 2 && IsWordChar(at(end));
  };
  // Whether a hyphenated word can be split after the hyphen at `i`.
  const auto hyphen_break = [&](int64_t i) {
    const bool letters_before =
        (IsLetter(at(i - 2)) && IsLetter(at(i - 1))) ||
        (IsLetter(at(i - 3)) && at(i - 2) == '-' && IsLetter(at(i - 1)));
    const bool letters_after =
        IsLetter(at(i + 1)) &&
        (IsLetter(at(i + 2)) || (at(i + 2) == '-' && IsLetter(at(i + 3))));
    return at(i) == '-' && letters_before && letters_after;
  };
  std::vector<std::string_view> chunks;
  int64_t start = 0;
  while (start < size) {
    int64_t end = start + 1;
    if (chars[start] == ' ') {
      while (at(end) == ' ') ++end;
    } else if (IsWordPunct(at(start - 1)) && dashes_then_word(start)) {
      // An em-dash between words: "--" in "a--b".
      while (at(end) == '-') ++end;
    } else {
      // The shortest word that ends before a hyphen to split at, a space, or
      // an em-dash.
      while (!hyphen_break(end) && end < size && chars[end] != ' ' &&
             !(IsWordPunct(at(end - 1)) && dashes_then_word(end))) {
        ++end;
      }
      if (hyphen_break(end)) ++end;
    }
    chunks.push_back(
        text.substr(offsets[start], offsets[end] - offsets[start]));
    start = end;
  }
  return chunks;
}

// Moves the part of `*chunk` that fits into `*line` (which is `line_length`
// long) when the chunk is longer than a whole line
// (TextWrapper._handle_long_word()).
void BreakLongWord(std::string_view* chunk, int width, int line_length,
                   std::vector<std::string_view>* line) {
  const int space_left = width < 1 ? 1 : width - line_length;
  size_t end = CodePointPrefixSize(*chunk, space_left);
  if (Length(*chunk) > space_left) {
    const size_t hyphen = chunk->substr(0, end).rfind('-');
    if (hyphen != std::string_view::npos && hyphen > 0 &&
        chunk->substr(0, hyphen).find_first_not_of('-') !=
            std::string_view::npos) {
      end = hyphen + 1;
    }
  }
  line->push_back(chunk->substr(0, end));
  chunk->remove_prefix(end);
}

}  // namespace

std::vector<std::string> Wrap(std::string_view text, int width,
                              std::string_view indent) {
  const std::string munged = MungeWhitespace(text);
  std::vector<std::string_view> chunks = SplitChunks(munged);
  const int line_width = width - Length(indent);
  std::vector<std::string> lines;
  size_t next = 0;
  while (next < chunks.size()) {
    if (!lines.empty() && IsBlank(chunks[next])) ++next;
    std::vector<std::string_view> line;
    int line_length = 0;
    while (next < chunks.size() &&
           line_length + Length(chunks[next]) <= line_width) {
      line_length += Length(chunks[next]);
      line.push_back(chunks[next]);
      ++next;
    }
    if (next < chunks.size() && Length(chunks[next]) > line_width) {
      BreakLongWord(&chunks[next], line_width, line_length, &line);
    }
    if (!line.empty() && IsBlank(line.back())) line.pop_back();
    if (!line.empty()) {
      std::string joined(indent);
      for (const std::string_view piece : line) joined.append(piece);
      lines.push_back(std::move(joined));
    }
  }
  return lines;
}

}  // namespace cc::util
