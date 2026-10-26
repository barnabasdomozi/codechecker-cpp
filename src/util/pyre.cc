#include "util/pyre.h"

#include <pcre2.h>
#include <stddef.h>
#include <stdint.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/result.h"

namespace cc::util {
namespace {

// Frees PCRE2 match data and compile contexts.
struct MatchDataDeleter {
  void operator()(pcre2_match_data* data) const { pcre2_match_data_free(data); }
};
struct CompileContextDeleter {
  void operator()(pcre2_compile_context* context) const {
    pcre2_compile_context_free(context);
  }
};

PCRE2_SPTR Bytes(std::string_view text) {
  return reinterpret_cast<PCRE2_SPTR>(text.data());
}

// Returns `pattern` with the escapes whose meaning differs between Python
// and PCRE2 rewritten: \Z (end of text) is PCRE2's \z, \v is the vertical
// tab character, and \uXXXX and \UXXXXXXXX are \x{...}.
std::string TranslatePattern(std::string_view pattern) {
  std::string result;
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] != '\\' || i + 1 == pattern.size()) {
      result += pattern[i];
      continue;
    }
    const char escape = pattern[++i];
    if (escape == 'Z') {
      result += "\\z";
    } else if (escape == 'v') {
      result += "\\x0b";
    } else if ((escape == 'u' || escape == 'U') &&
               i + (escape == 'u' ? 4 : 8) < pattern.size()) {
      const size_t digits = escape == 'u' ? 4 : 8;
      result += "\\x{" + std::string(pattern.substr(i + 1, digits)) + "}";
      i += digits;
    } else {
      result += '\\';
      result += escape;
    }
  }
  return result;
}

// Compiles `pattern` like Python's re.compile() of a str pattern: Unicode
// classes, and "." and "$" treating only "\n" as a line break.
Result<pcre2_code*> CompilePattern(std::string_view pattern) {
  const std::unique_ptr<pcre2_compile_context, CompileContextDeleter> context(
      pcre2_compile_context_create(nullptr));
  pcre2_set_newline(context.get(), PCRE2_NEWLINE_LF);
  const std::string translated = TranslatePattern(pattern);
  int error_code = 0;
  PCRE2_SIZE error_offset = 0;
  pcre2_code* code = pcre2_compile(
      Bytes(translated), translated.size(),
      PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF, &error_code,
      &error_offset, context.get());
  if (code == nullptr) {
    PCRE2_UCHAR message[256];
    pcre2_get_error_message(error_code, message, sizeof(message));
    return Result<pcre2_code*>(
        Error{.message = std::string(reinterpret_cast<char*>(message)) +
                         " at position " + std::to_string(error_offset)});
  }
  // Without JIT support the interpreter runs the pattern.
  pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
  return Result<pcre2_code*>(code);
}

}  // namespace

std::optional<std::string_view> RegexMatch::Group(int index) const {
  const size_t start = offsets_[2 * index];
  if (start == PCRE2_UNSET) return std::nullopt;
  return subject_.substr(start, offsets_[2 * index + 1] - start);
}

std::optional<std::string_view> RegexMatch::Group(
    std::string_view name) const {
  const std::string terminated(name);
  const int index = pcre2_substring_number_from_name(
      code_, reinterpret_cast<PCRE2_SPTR>(terminated.c_str()));
  if (index < 0) std::terminate();  // The program asked for a wrong name.
  return Group(index);
}

Regex::Regex(std::string_view pattern) {
  Result<pcre2_code*> code = CompilePattern(pattern);
  if (!code.ok()) std::terminate();
  code_.reset(code.value());
}

Result<Regex> Regex::Compile(std::string_view pattern) {
  Result<pcre2_code*> code = CompilePattern(pattern);
  if (!code.ok()) return Result<Regex>(code.error());
  return Result<Regex>(Regex(code.value()));
}

std::optional<RegexMatch> Regex::Match(std::string_view text) const {
  return Run(text, PCRE2_ANCHORED);
}

std::optional<RegexMatch> Regex::FullMatch(std::string_view text) const {
  return Run(text, PCRE2_ANCHORED | PCRE2_ENDANCHORED);
}

std::optional<RegexMatch> Regex::Search(std::string_view text) const {
  return Run(text, 0);
}

std::vector<RegexMatch> Regex::FindAll(std::string_view text) const {
  std::vector<RegexMatch> matches;
  size_t start = 0;
  while (start <= text.size()) {
    std::optional<RegexMatch> match = Run(text, 0, start);
    if (!match) break;
    // After an empty match the search goes on one character later.
    start = match->End() > match->Start() ? match->End() : match->End() + 1;
    while (start < text.size() &&
           (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
      ++start;
    }
    matches.push_back(std::move(*match));
  }
  return matches;
}

std::optional<RegexMatch> Regex::Run(std::string_view text, uint32_t options,
                                     size_t start) const {
  const std::unique_ptr<pcre2_match_data, MatchDataDeleter> data(
      pcre2_match_data_create_from_pattern(code_.get(), nullptr));
  // Errors other than "no match" (match limits) also count as no match.
  if (pcre2_match(code_.get(), Bytes(text), text.size(), start, options,
                  data.get(), nullptr) < 0) {
    return std::nullopt;
  }
  const uint32_t pairs = pcre2_get_ovector_count(data.get());
  const PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(data.get());
  uint32_t groups = 0;
  pcre2_pattern_info(code_.get(), PCRE2_INFO_CAPTURECOUNT, &groups);
  std::vector<size_t> offsets(2 * (groups + 1), PCRE2_UNSET);
  for (uint32_t i = 0; i < pairs && i <= groups; ++i) {
    offsets[2 * i] = ovector[2 * i];
    offsets[2 * i + 1] = ovector[2 * i + 1];
  }
  return RegexMatch(code_.get(), text, std::move(offsets));
}

std::string Regex::ReplaceAll(std::string_view text,
                              std::string_view replacement) const {
  constexpr uint32_t kOptions = PCRE2_SUBSTITUTE_GLOBAL |
                                PCRE2_SUBSTITUTE_LITERAL |
                                PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
  std::string result(text.size() + replacement.size() + 1, '\0');
  for (int attempt = 0; attempt < 2; ++attempt) {
    PCRE2_SIZE size = result.size();
    const int status = pcre2_substitute(
        code_.get(), Bytes(text), text.size(), 0, kOptions, nullptr, nullptr,
        Bytes(replacement), replacement.size(),
        reinterpret_cast<PCRE2_UCHAR*>(result.data()), &size);
    if (status >= 0) {
      result.resize(size);
      return result;
    }
    if (status != PCRE2_ERROR_NOMEMORY) break;
    // `size` is now the size the result needs, with its terminating zero.
    result.assign(size, '\0');
  }
  return std::string(text);
}

std::string RegexEscape(std::string_view text) {
  constexpr std::string_view kSpecial = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
  std::string result;
  for (const char c : text) {
    if (kSpecial.find(c) != std::string_view::npos) result += '\\';
    result += c;
  }
  return result;
}

}  // namespace cc::util
