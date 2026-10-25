#include "util/json.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "util/result.h"
#include "util/strings.h"
#include "util/unicode.h"

namespace cc::util {
namespace {

// Appends `text` as a JSON string with json.dumps()'s escapes
// (ensure_ascii=True).
void AppendString(std::string_view text, std::string* out) {
  *out += '"';
  size_t pos = 0;
  while (pos < text.size()) {
    // Text is valid UTF-8 (architecture.md §12); a stray byte is written as
    // U+FFFD.
    uint32_t c = NextCodePoint(text, &pos).value_or(0xFFFD);
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      case '\b':
        *out += "\\b";
        break;
      case '\f':
        *out += "\\f";
        break;
      default:
        if (c >= 0x20 && c <= 0x7E) {
          *out += static_cast<char>(c);
        } else if (c < 0x10000) {
          *out += std::format("\\u{:04x}", c);
        } else {
          c -= 0x10000;
          *out += std::format("\\u{:04x}\\u{:04x}", 0xD800 | (c >> 10),
                              0xDC00 | (c & 0x3FF));
        }
    }
  }
  *out += '"';
}

// Appends `value` as Python's float.__repr__() writes it: the shortest
// digits that read back to the same value, in positional notation if the
// decimal point is at most 16 digits right or 4 digits left of the first
// digit, else in exponent notation. json.dumps() spells the special values
// NaN, Infinity and -Infinity.
void AppendReal(double value, std::string* out) {
  if (std::isnan(value)) {
    *out += "NaN";
    return;
  }
  if (std::isinf(value)) {
    *out += value < 0 ? "-Infinity" : "Infinity";
    return;
  }
  if (std::signbit(value)) *out += '-';
  if (value == 0) {
    *out += "0.0";
    return;
  }
  char buffer[64];
  const std::to_chars_result result =
      std::to_chars(buffer, buffer + sizeof(buffer), std::abs(value),
                    std::chars_format::scientific);
  const std::string_view scientific(buffer, result.ptr - buffer);
  const size_t e = scientific.find('e');
  std::string digits;
  for (const char c : scientific.substr(0, e)) {
    if (c != '.') digits += c;
  }
  const int digit_count = static_cast<int>(digits.size());
  // The position of the decimal point relative to the first digit.
  const int point =
      static_cast<int>(ParseInt(scientific.substr(e + 1)).value_or(0)) + 1;
  if (point <= -4 || point > 16) {
    *out += digits[0];
    if (digit_count > 1) *out += "." + digits.substr(1);
    *out += std::format("e{}{:02}", point > 0 ? '+' : '-', std::abs(point - 1));
  } else if (point <= 0) {
    *out += "0." + std::string(-point, '0') + digits;
  } else if (point >= digit_count) {
    *out += digits + std::string(point - digit_count, '0') + ".0";
  } else {
    *out += digits.substr(0, point) + "." + digits.substr(point);
  }
}

void AppendValue(yyjson_mut_val* value, std::string* out) {
  switch (yyjson_mut_get_type(value)) {
    case YYJSON_TYPE_NULL:
      *out += "null";
      break;
    case YYJSON_TYPE_BOOL:
      *out += yyjson_mut_get_bool(value) ? "true" : "false";
      break;
    case YYJSON_TYPE_RAW:
      out->append(yyjson_mut_get_raw(value), yyjson_mut_get_len(value));
      break;
    case YYJSON_TYPE_NUM:
      if (yyjson_mut_get_subtype(value) == YYJSON_SUBTYPE_UINT) {
        *out += std::to_string(yyjson_mut_get_uint(value));
      } else if (yyjson_mut_get_subtype(value) == YYJSON_SUBTYPE_SINT) {
        *out += std::to_string(yyjson_mut_get_sint(value));
      } else {
        AppendReal(yyjson_mut_get_real(value), out);
      }
      break;
    case YYJSON_TYPE_STR:
      AppendString(std::string_view(yyjson_mut_get_str(value),
                                    yyjson_mut_get_len(value)),
                   out);
      break;
    case YYJSON_TYPE_ARR: {
      yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(value);
      *out += '[';
      for (size_t i = 0; yyjson_mut_val* item = yyjson_mut_arr_iter_next(&iter);
           ++i) {
        if (i > 0) *out += ", ";
        AppendValue(item, out);
      }
      *out += ']';
      break;
    }
    case YYJSON_TYPE_OBJ: {
      yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
      *out += '{';
      for (size_t i = 0; yyjson_mut_val* key = yyjson_mut_obj_iter_next(&iter);
           ++i) {
        if (i > 0) *out += ", ";
        AppendValue(key, out);
        *out += ": ";
        AppendValue(yyjson_mut_obj_iter_get_val(key), out);
      }
      *out += '}';
      break;
    }
    default:
      // A value without a type: a document built wrong.
      std::terminate();
  }
}

}  // namespace

Result<JsonDocument> ParseJson(std::string_view text) {
  yyjson_read_err error = {};
  // yyjson changes the input only with YYJSON_READ_INSITU.
  yyjson_doc* document = yyjson_read_opts(
      const_cast<char*>(text.data()), text.size(),
      YYJSON_READ_ALLOW_INF_AND_NAN | YYJSON_READ_BIGNUM_AS_RAW, nullptr,
      &error);
  if (document != nullptr) return Result<JsonDocument>(JsonDocument(document));
  // The position in code points, as Python's JSONDecodeError counts it.
  const std::string_view before =
      text.substr(0, std::min(error.pos, text.size()));
  const size_t line_start = before.rfind('\n') + 1;  // 0 if there is none.
  return Result<JsonDocument>(Error{
      .message = std::format(
          "{}: line {} column {} (char {})",
          error.msg != nullptr ? error.msg : "Expecting value",
          std::ranges::count(before, '\n') + 1,
          CodePointCount(before.substr(line_start)) + 1,
          CodePointCount(before))});
}

MutableJsonDocument NewJsonDocument() {
  return MutableJsonDocument(yyjson_mut_doc_new(nullptr));
}

std::string DumpJson(yyjson_mut_val* value) {
  std::string out;
  AppendValue(value, &out);
  return out;
}

}  // namespace cc::util
