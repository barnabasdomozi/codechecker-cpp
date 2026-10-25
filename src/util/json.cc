#include "util/json.h"

#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
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

// Appends `value` as json.dumps() writes a float: like repr(), but with the
// special values spelled NaN, Infinity and -Infinity.
void AppendReal(double value, std::string* out) {
  if (std::isnan(value)) {
    *out += "NaN";
  } else if (std::isinf(value)) {
    *out += value < 0 ? "-Infinity" : "Infinity";
  } else {
    *out += FloatRepr(value);
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

yyjson_val* JsonGet(yyjson_val* value, std::string_view key) {
  if (!yyjson_is_obj(value)) return nullptr;
  yyjson_val* found = nullptr;
  yyjson_obj_iter iter = yyjson_obj_iter_with(value);
  while (yyjson_val* name = yyjson_obj_iter_next(&iter)) {
    if (std::string_view(yyjson_get_str(name), yyjson_get_len(name)) == key) {
      found = yyjson_obj_iter_get_val(name);
    }
  }
  return found;
}

std::optional<int64_t> JsonToInt(yyjson_val* value) {
  switch (yyjson_get_type(value)) {
    case YYJSON_TYPE_BOOL:
      return yyjson_get_bool(value) ? 1 : 0;
    case YYJSON_TYPE_NUM:
      if (yyjson_is_real(value)) {
        const double real = yyjson_get_real(value);
        if (!std::isfinite(real)) return std::nullopt;
        return static_cast<int64_t>(real);
      }
      return yyjson_get_int(value);
    case YYJSON_TYPE_STR:
      return ParseInt(
          std::string_view(yyjson_get_str(value), yyjson_get_len(value)));
    default:
      return std::nullopt;
  }
}

std::string JsonToStr(yyjson_val* value) {
  if (value == nullptr) return "None";
  switch (yyjson_get_type(value)) {
    case YYJSON_TYPE_STR:
      return std::string(yyjson_get_str(value), yyjson_get_len(value));
    case YYJSON_TYPE_NULL:
      return "None";
    case YYJSON_TYPE_BOOL:
      return yyjson_get_bool(value) ? "True" : "False";
    case YYJSON_TYPE_NUM:
      if (yyjson_is_real(value)) return FloatRepr(yyjson_get_real(value));
      if (yyjson_is_uint(value)) return std::to_string(yyjson_get_uint(value));
      return std::to_string(yyjson_get_sint(value));
    case YYJSON_TYPE_RAW:
      return std::string(yyjson_get_raw(value), yyjson_get_len(value));
    default: {
      const MutableJsonDocument copy = NewJsonDocument();
      return DumpJson(yyjson_val_mut_copy(copy.get(), value));
    }
  }
}

Result<yyjson_val*> JsonSubscript(yyjson_val* value, std::string_view key) {
  if (!yyjson_is_obj(value)) {
    return Result<yyjson_val*>(Error{
        .message = value == nullptr || yyjson_is_null(value)
                       ? "TypeError: 'NoneType' object is not subscriptable"
                       : "TypeError: indices must be integers or slices, not "
                         "str"});
  }
  yyjson_val* member = JsonGet(value, key);
  if (member == nullptr) {
    return Result<yyjson_val*>(Error{.message = "KeyError: " + Repr(key)});
  }
  return Result<yyjson_val*>(member);
}

Result<int64_t> JsonSubscriptInt(yyjson_val* value, std::string_view key) {
  const Result<yyjson_val*> member = JsonSubscript(value, key);
  if (!member.ok()) return Result<int64_t>(member.error());
  const std::optional<int64_t> number = JsonToInt(member.value());
  if (!number) {
    return Result<int64_t>(Error{
        .message = "ValueError: invalid literal for int() with base 10: " +
                   Repr(JsonToStr(member.value()))});
  }
  return Result<int64_t>(*number);
}

}  // namespace cc::util
