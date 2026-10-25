#include "report/converter/sanitizers.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "report/converter/line_parser.h"
#include "report/model/report.h"
#include "util/pypath.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::report {
namespace {

// The settings of one sanitizer's parser (Python: the SANParser subclasses).
struct Sanitizer {
  std::string_view checker_name;
  // The header line of a report, with a "message" group.
  const util::Regex* header;
  bool skip_line_after_header;
  // Which stack frame the report is at: the outermost one (the last after
  // the events are reversed), or for ThreadSanitizer the innermost one.
  bool innermost_frame;
};

// A check name and the patterns of its messages (Python: check_msg_pairs
// of AddressSanitizer and checks of UndefinedBehaviorSanitizer).
struct Check {
  std::string_view name;
  std::vector<std::string_view> patterns;
};

// Returns the AddressSanitizer checks, in the order they are tried. The
// patterns are copied from sanitizers/address/parser.py as they are.
const std::vector<Check>& AsanChecks() {
  static const std::vector<Check>& checks = *new std::vector<Check>{
      {"double-free", {"attempting double-free on .* in thread .*:"}},
      {"new-delete-type-mismatch",
       {"new-delete-type-mismatch on .* in thread .*:"}},
      {"bad-free",
       {"attempting free on address which was not malloc()-ed: .* in "
        "thread .*"}},
      {"alloc-dealloc-mismatch", {"alloc-dealloc-mismatch (.* vs .*) on .*"}},
      {"bad-malloc_usable_size",
       {"attempting to call malloc_usable_size() for pointer which is not "
        "not owned: .*"}},
      {"bad-__sanitizer_get_allocated_size",
       {"attempting to call __sanitizer_get_allocated_size() for pointer "
        "which is not owned: .*"}},
      {"calloc-overflow",
       {"calloc parameters overflow: count * size (.* * .*) cannot be "
        "represented in type size_t (thread .*)"}},
      {"reallocarray-overflow",
       {"reallocarray parameters overflow: count * size (.* * .*) cannot "
        "be represented in type size_t (thread .*)"}},
      {"pvalloc-overflow",
       {"pvalloc parameters overflow: size 0x.* rounded up to system page "
        "size 0x.* cannot be represented in type size_t (thread .*)"}},
      {"invalid-allocation-alignment",
       {"invalid allocation alignment: .*, alignment must be a power of "
        "two (thread .*)"}},
      {"invalid-aligned-alloc-alignment",
       {"invalid alignment requested in aligned_alloc: .* the requested "
        "size 0x.* must be a multiple of alignment (thread .*)"}},
      {"invalid-posix-memalign-alignment",
       {"invalid alignment requested in posix_memalign: .*, alignment "
        "must be a power of two and a multiple of sizeof(void*) == .* "
        "(thread .*)"}},
      {"allocation-size-too-big",
       {"requested allocation size 0x.* (0x.* after adjustments for "
        "alignment, red zones etc.) exceeds maximum supported size of "
        "0x.* (thread .*)"}},
      {"rss-limit-exceeded",
       {"specified RSS limit exceeded, currently set to "
        "soft_rss_limit_mb=.*"}},
      {"out-of-memory", {"allocator is trying to allocate 0x.* bytes"}},
      {"string-function-memory-ranges-overlap",
       {".*: memory ranges .* and .* overlap"}},
      {"negative-size-param", {"negative-size-param: (size=.*)"}},
      {"bad-__sanitizer_annotate_contiguous_container",
       {"bad parameters to __sanitizer_annotate_contiguous_container:"}},
      {"bad-__sanitizer_annotate_double_ended_contiguous_container",
       {"bad parameters to "
        "__sanitizer_annotate_double_ended_contiguous_container:"}},
      {"odr-violation", {"odr-violation (.*):"}},
      {"invalid-pointer-pair", {"invalid-pointer-pair: .* .*"}},
      {"generic-error", {".* on address .* at pc .* bp .* sp .*"}},
  };
  return checks;
}

// Returns the UndefinedBehaviorSanitizer checks, in the order they are
// tried, copied from sanitizers/ub/parser.py.
const std::vector<Check>& UbsanChecks() {
  static const std::vector<Check>& checks = *new std::vector<Check>{
      {"alignment",
       {".* misaligned address .* for type .*, which requires .* byte "
        "alignment",
        "assumption of .* byte alignment for pointer of type .* failed",
        "assumption of .* byte alignment (with offset of .* byte) for "
        "pointer of type .* failed"}},
      {"bool",
       {"load of value .*, which is not a valid value for type .*bool.*"}},
      {"builtin", {"passing zero to .*, which is not a valid argument"}},
      {"bounds", {"index .* out of bounds for type .*"}},
      {"enum", {"load of value .*, which is not a valid value for type .*"}},
      {"float-cast-overflow",
       {".* is outside the range of representable values of type .*"}},
      {"integer-divide-by-zero-or-float-divide-by-zero", {"division by zero"}},
      {"implicit-signed-integer-truncation",
       {"implicit conversion from type .* of value .* (.*-bit, signed) to "
        "type .* changed the value to .* (.*-bit, signed)"}},
      {"implicit-unsigned-integer-truncation",
       {"implicit conversion from type .* of value .* (.*-bit, unsigned) to "
        "type .* changed the value to .* (.*-bit, unsigned)"}},
      {"implicit-integer-sign-change",
       {"implicit conversion from type .* of value .* (.*-bit, .*signed) to "
        "type .* changed the value to .* (.*-bit, .*signed)"}},
      {"nonnull-attribute-or-nullability-arg",
       {"null pointer passed as argument .*, which is declared to never be "
        "null"}},
      {"null-or-nullability-assign", {".* null pointer of type .*"}},
      {"nullability-return-or-returns-nonnull-attribute",
       {"null pointer returned from function declared to never return "
        "null"}},
      {"objc-cast",
       {"invalid ObjC cast, object is a '.*', but expected a .*"}},
      {"object-size",
       {".* address .* with insufficient space for an object of type .*"}},
      {"pointer-overflow",
       {"applying zero offset to null pointer",
        "applying non-zero offset .* to null pointer",
        "applying non-zero offset to non-null pointer .* produced null "
        "pointer",
        "addition of unsigned offset to .* overflowed to .*",
        "subtraction of unsigned offset from .* overflowed to .*",
        "pointer index expression with base .* overflowed to .*"}},
      {"return",
       {"execution reached the end of a value-returning function without "
        "returning a value"}},
      {"shift",
       {"shift exponent .* is negative",
        "shift exponent .* is too large for .*-bit type .*",
        "left shift of negative value .*"}},
      {"signed-integer-overflow",
       {"signed integer overflow: .* .* .* cannot be represented in type .*",
        "negation of .* cannot be represented in type .*; cast to an "
        "unsigned type to negate this value to itself",
        "division of .* by -1 cannot be represented in type .*"}},
      {"unreachable", {"execution reached an unreachable program point"}},
      {"unsigned-integer-overflow",
       {"unsigned integer overflow: .* .* .* cannot be represented in type "
        ".*",
        "negation of .* cannot be represented in type .*",
        "left shift of .* by .* places cannot be represented in type .*"}},
      {"vla-bound",
       {"variable length array bound evaluates to non-positive value .*"}},
      {"vptr",
       {".* address .* which does not point to an object of type .*"}},
  };
  return checks;
}

// Returns "<checker>.<check>" for the first check with a pattern found in
// `message`, else `checker` (Python: deduce_checker_name()).
std::string DeduceCheckerName(std::string_view checker,
                              const std::vector<Check>& checks,
                              std::string_view message) {
  for (const Check& check : checks) {
    for (const std::string_view pattern : check.patterns) {
      // Compiled for each message: sanitizer outputs have few reports.
      if (util::Regex(pattern).Search(message)) {
        return std::string(checker) + "." + std::string(check.name);
      }
    }
  }
  return std::string(checker);
}

// Returns the event of a stack frame line if it names a source file that
// exists (Python: parse_stack_trace_line()).
std::optional<BugPathEvent> StackFrameEvent(const std::string& line) {
  static const util::Regex& kFile = *new util::Regex(
      R"((?P<path>[\S]+?):(?P<line>\d+)(:(?P<column>\d+))?)");
  const std::optional<util::RegexMatch> match = kFile.Search(line);
  if (!match) return std::nullopt;
  const std::string path(match->Group("path").value_or(""));
  if (path.empty() || !util::pypath::Exists(path)) return std::nullopt;
  const std::optional<std::string_view> column = match->Group("column");
  return BugPathEvent{.message = std::string(util::RightStrip(line)),
                      .file = MakeFile(util::pypath::Abspath(path)),
                      .line = ToInt(match->Group("line").value_or("")),
                      .column = column ? ToInt(*column) : 0};
}

// The stack frame lines of a report and the events made from them.
struct StackTrace {
  std::vector<std::string> lines;
  std::vector<BugPathEvent> events;  // Outermost frame first.
};

// Takes stack frame lines from `line` on while `is_frame` accepts them, and
// sets `line` to the first line after them (Python: parse_stack_trace()).
template <typename Predicate>
StackTrace ParseStackTrace(LineReader* reader, std::string* line,
                           Predicate is_frame) {
  StackTrace trace;
  while (is_frame(*line)) {
    if (std::optional<BugPathEvent> event = StackFrameEvent(*line)) {
      trace.events.push_back(std::move(*event));
    }
    trace.lines.push_back(*line);
    *line = reader->NextOrEmpty();
  }
  std::ranges::reverse(trace.events);
  return trace;
}

// Python: SANParser.create_report().
Report CreateReport(StackTrace trace, const File& file, int line, int column,
                    const std::string& message,
                    const std::string& checker_name) {
  static const util::Regex& kAddress = *new util::Regex("0x[0-9a-fA-F]+");
  trace.events.push_back(
      {.message = message, .file = file, .line = line, .column = column});
  Report report = MakeReport(file, line, column, message, checker_name,
                             std::move(trace.events));
  if (!trace.lines.empty()) {
    report.notes.push_back({.message = util::Join(trace.lines, ""),
                            .file = file,
                            .line = line,
                            .column = column});
  }
  report.static_message = kAddress.ReplaceAll(message, "");
  return report;
}

// Returns the reports of AddressSanitizer, MemorySanitizer,
// ThreadSanitizer or LeakSanitizer output (Python: SANParser).
Result<std::vector<Report>> GetSanitizerReports(const std::string& path,
                                                const Sanitizer& sanitizer) {
  return ParseLines(path, [&sanitizer](LineReader* reader,
                                       const std::string& header,
                                       std::vector<Report>* reports) {
    using Next = Result<std::optional<std::string>>;
    const std::optional<util::RegexMatch> match =
        sanitizer.header->Match(header);
    if (!match) return Next(reader->Next());
    const std::string message(
        util::Strip(match->Group("message").value_or("")));
    std::string line = sanitizer.skip_line_after_header
                           ? reader->NextOrEmpty()
                           : header;
    StackTrace trace = ParseStackTrace(reader, &line, [](const std::string& l) {
      return !util::Strip(l).empty();
    });
    // A header without usable frames gives no report, and the line after
    // the frames is skipped, as in the Python version.
    if (trace.events.empty()) return Next(reader->Next());
    const BugPathEvent main = sanitizer.innermost_frame ? trace.events.front()
                                                        : trace.events.back();
    std::string checker_name(sanitizer.checker_name);
    if (checker_name == "AddressSanitizer") {
      checker_name = DeduceCheckerName(checker_name, AsanChecks(), message);
    }
    reports->push_back(CreateReport(std::move(trace), main.file, main.line,
                                    main.column, message, checker_name));
    return Next(line);
  });
}

}  // namespace

Result<std::vector<Report>> GetAsanReports(const std::string& path) {
  static const util::Regex& kHeader = *new util::Regex(
      R"(==(?P<code>\d+)==(ERROR|WARNING): AddressSanitizer: )"
      R"((?P<message>[\S \t]+))");
  return GetSanitizerReports(path,
                             {.checker_name = "AddressSanitizer",
                              .header = &kHeader,
                              .skip_line_after_header = true,
                              .innermost_frame = false});
}

Result<std::vector<Report>> GetMsanReports(const std::string& path) {
  static const util::Regex& kHeader = *new util::Regex(
      R"(==(?P<code>\d+)==(ERROR|WARNING): MemorySanitizer: )"
      R"((?P<message>[\S \t]+))");
  return GetSanitizerReports(path,
                             {.checker_name = "MemorySanitizer",
                              .header = &kHeader,
                              .skip_line_after_header = true,
                              .innermost_frame = false});
}

Result<std::vector<Report>> GetTsanReports(const std::string& path) {
  static const util::Regex& kHeader = *new util::Regex(
      R"(==(?P<code>\d+)==(ERROR|WARNING): ThreadSanitizer: )"
      R"((?P<message>[\S \t]+))");
  return GetSanitizerReports(path,
                             {.checker_name = "ThreadSanitizer",
                              .header = &kHeader,
                              .skip_line_after_header = true,
                              .innermost_frame = true});
}

Result<std::vector<Report>> GetLsanReports(const std::string& path) {
  static const util::Regex& kHeader = *new util::Regex(
      R"((?P<message>(Ind|D)irect leak of \d+ byte\(s\) )"
      R"(in \d+ object\(s\) allocated from:))");
  return GetSanitizerReports(path,
                             {.checker_name = "LeakSanitizer",
                              .header = &kHeader,
                              .skip_line_after_header = false,
                              .innermost_frame = false});
}

Result<std::vector<Report>> GetUbsanReports(const std::string& path) {
  static const util::Regex& kHeader = *new util::Regex(
      R"(^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): runtime error: )"
      R"((?P<message>[\S \t]+))");
  static const util::Regex& kFrame = *new util::Regex(R"(^\s+#\d+)");
  return ParseLines(path, [](LineReader* reader, const std::string& header,
                             std::vector<Report>* reports) {
    using Next = Result<std::optional<std::string>>;
    const std::optional<util::RegexMatch> match = kHeader.Match(header);
    if (!match) return Next(reader->Next());
    const File file = MakeFile(
        util::pypath::Abspath(match->Group("path").value_or("")));
    const int line_number = ToInt(match->Group("line").value_or(""));
    const int column = ToInt(match->Group("column").value_or(""));
    std::string line = reader->NextOrEmpty();
    StackTrace trace = ParseStackTrace(reader, &line, [](const std::string& l) {
      return kFrame.Match(l).has_value();
    });
    const std::string message(
        util::Strip(match->Group("message").value_or("")));
    reports->push_back(CreateReport(
        std::move(trace), file, line_number, column, message,
        DeduceCheckerName("UndefinedBehaviorSanitizer", UbsanChecks(),
                          message)));
    return Next(line);
  });
}

}  // namespace cc::report
