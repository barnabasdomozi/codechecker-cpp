#include "util/fnmatch.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/pyre.h"
#include "util/result.h"
#include "util/unicode.h"

namespace cc::util {
namespace {

using CodePoints = std::vector<uint32_t>;

CodePoints ToCodePoints(std::string_view text) {
  CodePoints result;
  size_t pos = 0;
  while (pos < text.size()) {
    result.push_back(NextCodePoint(text, &pos).value_or(0xFFFD));
  }
  return result;
}

// Returns the contents of a character class [...] (the text between the
// brackets of the pattern) as Python's _translate() writes it.
std::string TranslateClass(const CodePoints& pat, size_t i, size_t j) {
  // The class as chunks separated by range hyphens.
  std::vector<CodePoints> chunks;
  const auto escape_into = [](const CodePoints& chunk, bool escape_hyphens,
                              std::string* out) {
    for (const uint32_t c : chunk) {
      if (c == '\\') {
        *out += "\\\\";
      } else if (c == '-' && escape_hyphens) {
        *out += "\\-";
      } else {
        AppendUtf8(c, out);
      }
    }
  };
  std::string stuff;
  bool has_hyphen = false;
  for (size_t k = i; k < j; ++k) has_hyphen = has_hyphen || pat[k] == '-';
  if (!has_hyphen) {
    escape_into(CodePoints(pat.begin() + i, pat.begin() + j),
                /*escape_hyphens=*/false, &stuff);
  } else {
    size_t k = pat[i] == '!' ? i + 2 : i + 1;
    while (true) {
      while (k < j && pat[k] != '-') ++k;
      if (k >= j) break;
      chunks.emplace_back(pat.begin() + i, pat.begin() + k);
      i = k + 1;
      k = k + 3;
    }
    const CodePoints chunk(pat.begin() + i, pat.begin() + j);
    if (!chunk.empty()) {
      chunks.push_back(chunk);
    } else {
      chunks.back().push_back('-');
    }
    // Remove empty ranges, which are invalid in a regular expression.
    for (size_t m = chunks.size() - 1; m > 0; --m) {
      if (chunks[m - 1].empty() || chunks[m].empty()) continue;
      if (chunks[m - 1].back() > chunks[m].front()) {
        chunks[m - 1].pop_back();
        chunks[m - 1].insert(chunks[m - 1].end(), chunks[m].begin() + 1,
                             chunks[m].end());
        chunks.erase(chunks.begin() + static_cast<ptrdiff_t>(m));
      }
    }
    for (size_t m = 0; m < chunks.size(); ++m) {
      if (m > 0) stuff += '-';
      escape_into(chunks[m], /*escape_hyphens=*/true, &stuff);
    }
  }
  // Escape set operations (&&, ~~ and ||).
  std::string result;
  for (const char c : stuff) {
    if (c == '&' || c == '~' || c == '|') result += '\\';
    result += c;
  }
  return result;
}

}  // namespace

std::string FnmatchTranslate(std::string_view pattern) {
  const CodePoints pat = ToCodePoints(pattern);
  // The translated parts; nullopt stands for a "*".
  std::vector<std::optional<std::string>> parts;
  size_t i = 0;
  const size_t n = pat.size();
  while (i < n) {
    const uint32_t c = pat[i++];
    if (c == '*') {
      // Consecutive stars are compressed into one.
      if (parts.empty() || parts.back()) parts.push_back(std::nullopt);
    } else if (c == '?') {
      parts.push_back(".");
    } else if (c == '[') {
      size_t j = i;
      if (j < n && pat[j] == '!') ++j;
      if (j < n && pat[j] == ']') ++j;
      while (j < n && pat[j] != ']') ++j;
      if (j >= n) {
        parts.push_back("\\[");
        continue;
      }
      std::string stuff = TranslateClass(pat, i, j);
      i = j + 1;
      if (stuff.empty()) {
        parts.push_back("(?!)");  // An empty range never matches.
      } else if (stuff == "!") {
        parts.push_back(".");  // A negated empty range matches anything.
      } else {
        if (stuff[0] == '!') {
          stuff[0] = '^';
        } else if (stuff[0] == '^' || stuff[0] == '[') {
          stuff.insert(0, "\\");
        }
        parts.push_back("[" + stuff + "]");
      }
    } else {
      std::string character;
      AppendUtf8(c, &character);
      parts.push_back(RegexEscape(character));
    }
  }

  // Python: _join_translated_parts().
  std::string result;
  size_t k = 0;
  while (k < parts.size() && parts[k]) result += *parts[k++];
  while (k < parts.size()) {
    ++k;  // The star.
    if (k == parts.size()) {
      result += ".*";
      break;
    }
    std::string fixed;
    while (k < parts.size() && parts[k]) fixed += *parts[k++];
    if (k == parts.size()) {
      result += ".*" + fixed;
    } else {
      result += "(?>.*?" + fixed + ")";
    }
  }
  return "(?s:" + result + ")\\Z";
}

bool FnMatch(std::string_view name, std::string_view pattern) {
  const Result<Regex> regex = Regex::Compile(FnmatchTranslate(pattern));
  return regex.ok() && regex.value().Match(name).has_value();
}

}  // namespace cc::util
