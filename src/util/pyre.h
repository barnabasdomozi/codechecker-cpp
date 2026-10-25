// Regular expressions with the syntax and results of Python's re module on
// UTF-8 text, run by PCRE2 (libraries.md, "Regular expressions: PCRE2").
//
//   static const util::Regex& kLine = *new util::Regex(R"((?P<line>\d+):)");
//   if (std::optional<util::RegexMatch> match = kLine.Match(text)) {
//     Use(match->Group("line"));
//   }
//
// A Regex can be used from several threads at the same time.

#ifndef CC_UTIL_PYRE_H_
#define CC_UTIL_PYRE_H_

#include <pcre2.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/result.h"

namespace cc::util {

namespace internal_pyre {

// Frees compiled PCRE2 patterns.
struct CodeDeleter {
  void operator()(pcre2_code* code) const { pcre2_code_free(code); }
};

}  // namespace internal_pyre

// A successful match (Python: re.Match). It refers to the matched text and
// the Regex, which must outlive it.
class RegexMatch {
 public:
  RegexMatch(const RegexMatch& other) = default;
  RegexMatch& operator=(const RegexMatch& other) = default;

  // Returns the text of group `index` (0 is the whole match), or nullopt if
  // the group did not take part in the match (Python: match.group()).
  std::optional<std::string_view> Group(int index) const;
  // Returns the text of the named group `name`, which must exist.
  std::optional<std::string_view> Group(std::string_view name) const;
  // Returns the number of capturing groups (Python: len(match.groups())).
  int GroupCount() const { return static_cast<int>(offsets_.size() / 2) - 1; }
  // Returns the byte offset where the whole match starts or ends.
  size_t Start() const { return offsets_[0]; }
  size_t End() const { return offsets_[1]; }

 private:
  friend class Regex;

  RegexMatch(const pcre2_code* code, std::string_view subject,
             std::vector<size_t> offsets)
      : code_(code), subject_(subject), offsets_(std::move(offsets)) {}

  const pcre2_code* code_;
  std::string_view subject_;
  // The start and end of each group; PCRE2_UNSET for groups that did not
  // take part in the match.
  std::vector<size_t> offsets_;
};

// A compiled regular expression (Python: re.compile(pattern)). Patterns use
// Python's syntax, including (?P<name>...), \Z, \v and \uXXXX.
class Regex {
 public:
  // Compiles `pattern`, which comes from the program itself and must be
  // valid: an invalid pattern is an internal error (std::terminate()).
  explicit Regex(std::string_view pattern);

  Regex(Regex&& other) = default;
  Regex& operator=(Regex&& other) = default;

  // Compiles `pattern`, for example one given by the user. The error message
  // is PCRE2's.
  static Result<Regex> Compile(std::string_view pattern);

  // Python: regex.match(text), anchored at the start of `text`.
  std::optional<RegexMatch> Match(std::string_view text) const;
  // Python: regex.fullmatch(text).
  std::optional<RegexMatch> FullMatch(std::string_view text) const;
  // Python: regex.search(text).
  std::optional<RegexMatch> Search(std::string_view text) const;
  // Returns `text` with every match replaced by `replacement`, which is used
  // as it is: it has no backslash escapes or group references (Python:
  // regex.sub(replacement, text)).
  std::string ReplaceAll(std::string_view text,
                         std::string_view replacement) const;

 private:
  explicit Regex(pcre2_code* code) : code_(code) {}

  std::optional<RegexMatch> Run(std::string_view text, uint32_t options) const;

  std::unique_ptr<pcre2_code, internal_pyre::CodeDeleter> code_;
};

}  // namespace cc::util

#endif  // CC_UTIL_PYRE_H_
