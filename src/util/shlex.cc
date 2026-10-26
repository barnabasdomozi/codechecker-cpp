#include "util/shlex.h"

#include <stddef.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/result.h"

namespace cc::util {
namespace {

bool IsShlexSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsQuote(char c) { return c == '\'' || c == '"'; }

// The states of Python's shlex.read_token(): between tokens, in a word, in
// quotes (the quote character), after a backslash.
enum class State { kSpace, kWord, kQuote, kEscape };

}  // namespace

Result<std::vector<std::string>> ShlexSplit(std::string_view text) {
  using Args = std::vector<std::string>;
  Args args;
  size_t pos = 0;
  while (true) {
    std::string token;
    bool quoted = false;
    State state = State::kSpace;
    char quote = '\0';
    // Where to return after an escaped character: the word or the quote.
    State escaped_state = State::kWord;
    while (true) {
      const bool at_end = pos >= text.size();
      const char c = at_end ? '\0' : text[pos++];
      if (state == State::kSpace) {
        if (at_end) break;
        if (IsShlexSpace(c)) {
          if (!token.empty() || quoted) break;
          continue;
        }
        if (c == '\\') {
          escaped_state = State::kWord;
          state = State::kEscape;
        } else if (IsQuote(c)) {
          quote = c;
          state = State::kQuote;
        } else {
          token = c;
          state = State::kWord;
        }
      } else if (state == State::kQuote) {
        quoted = true;
        if (at_end) {
          return Result<Args>(Error{.message = "No closing quotation"});
        }
        if (c == quote) {
          state = State::kWord;
        } else if (c == '\\' && quote == '"') {
          escaped_state = State::kQuote;
          state = State::kEscape;
        } else {
          token += c;
        }
      } else if (state == State::kEscape) {
        if (at_end) {
          return Result<Args>(Error{.message = "No escaped character"});
        }
        // In quotes only the quote and the backslash itself can be escaped.
        if (escaped_state == State::kQuote && c != '\\' && c != quote) {
          token += '\\';
        }
        token += c;
        state = escaped_state;
      } else {
        if (at_end) break;
        if (IsShlexSpace(c)) {
          state = State::kSpace;
          if (!token.empty() || quoted) break;
        } else if (IsQuote(c)) {
          quote = c;
          state = State::kQuote;
        } else if (c == '\\') {
          escaped_state = State::kWord;
          state = State::kEscape;
        } else {
          token += c;
        }
      }
    }
    if (token.empty() && !quoted) break;
    args.push_back(std::move(token));
  }
  return Result<Args>(std::move(args));
}

std::string ShlexQuote(std::string_view text) {
  if (text.empty()) return "''";
  bool safe = true;
  for (const char c : text) {
    const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
    if (!word && std::string_view("@%+=:,./-").find(c) ==
                     std::string_view::npos) {
      safe = false;
      break;
    }
  }
  if (safe) return std::string(text);
  std::string result = "'";
  for (const char c : text) {
    if (c == '\'') {
      result += "'\"'\"'";
    } else {
      result += c;
    }
  }
  return result + "'";
}

std::string ShlexJoin(const std::vector<std::string>& args) {
  std::string result;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) result += ' ';
    result += ShlexQuote(args[i]);
  }
  return result;
}

}  // namespace cc::util
