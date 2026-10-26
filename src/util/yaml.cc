#include "util/yaml.h"

#include <stddef.h>
#include <stdint.h>
#include <yaml.h>
#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "util/json.h"
#include "util/pyre.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::util {
namespace {

constexpr std::string_view kTagPrefix = "tag:yaml.org,2002:";

// The type of a scalar, decided by its tag or by PyYAML's implicit
// resolvers.
enum class ScalarType { kString, kNull, kBool, kInt, kFloat };

// Returns the type PyYAML's Resolver gives the plain scalar `value`: the
// implicit resolvers registered for its first character, in the order
// resolver.py adds them.
ScalarType ResolvePlain(std::string_view value) {
  static const Regex& kBool = *new Regex(
      "^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE"
      "|on|On|ON|off|Off|OFF)$");
  static const Regex& kFloat = *new Regex(
      R"(^(?:[-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)?)"
      R"(|\.[0-9][0-9_]*(?:[eE][-+][0-9]+)?)"
      R"(|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*)"
      R"(|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN))$)");
  static const Regex& kInt = *new Regex(
      R"(^(?:[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*))"
      R"(|[-+]?0x[0-9a-fA-F_]+|[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+)$)");
  static const Regex& kNull = *new Regex("^(?:~|null|Null|NULL|)$");
  const char first = value.empty() ? '\0' : value[0];
  const auto starts_with_one_of = [first](std::string_view chars) {
    return first != '\0' && chars.find(first) != std::string_view::npos;
  };
  if (starts_with_one_of("yYnNtTfFoO") && kBool.Match(value)) {
    return ScalarType::kBool;
  }
  if (starts_with_one_of("-+0123456789.") && kFloat.Match(value)) {
    return ScalarType::kFloat;
  }
  if (starts_with_one_of("-+0123456789") && kInt.Match(value)) {
    return ScalarType::kInt;
  }
  if ((first == '\0' || starts_with_one_of("~nN")) && kNull.Match(value)) {
    return ScalarType::kNull;
  }
  return ScalarType::kString;
}

// Returns the sign of `value` (+1 or -1) and removes it.
int TakeSign(std::string* value) {
  int sign = 1;
  if (!value->empty() && (value->front() == '-' || value->front() == '+')) {
    if (value->front() == '-') sign = -1;
    value->erase(0, 1);
  }
  return sign;
}

// Python: SafeConstructor.construct_yaml_int(). nullopt if the value does
// not fit 64 bits.
std::optional<int64_t> ConstructInt(std::string value) {
  std::erase(value, '_');
  const int sign = TakeSign(&value);
  int base = 10;
  std::string_view digits = value;
  if (value == "0") return 0;
  if (value.starts_with("0b")) {
    base = 2;
    digits.remove_prefix(2);
  } else if (value.starts_with("0x")) {
    base = 16;
    digits.remove_prefix(2);
  } else if (value.starts_with('0')) {
    base = 8;
  } else if (value.find(':') != std::string::npos) {
    int64_t result = 0;
    size_t pos = 0;
    while (pos <= value.size()) {
      size_t colon = value.find(':', pos);
      if (colon == std::string::npos) colon = value.size();
      const std::optional<int64_t> part =
          ParseInt(std::string_view(value).substr(pos, colon - pos));
      if (!part) return std::nullopt;
      result = result * 60 + *part;
      pos = colon + 1;
    }
    return sign * result;
  }
  int64_t result = 0;
  const std::from_chars_result parsed = std::from_chars(
      digits.data(), digits.data() + digits.size(), result, base);
  if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return sign * result;
}

// Python: SafeConstructor.construct_yaml_float().
double ConstructFloat(std::string value) {
  std::erase(value, '_');
  std::ranges::transform(value, value.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  const int sign = TakeSign(&value);
  if (value == ".inf") return sign * std::numeric_limits<double>::infinity();
  if (value == ".nan") return std::numeric_limits<double>::quiet_NaN();
  double result = 0;
  if (value.find(':') != std::string::npos) {
    size_t pos = 0;
    while (pos <= value.size()) {
      size_t colon = value.find(':', pos);
      if (colon == std::string::npos) colon = value.size();
      double part = 0;
      std::from_chars(value.data() + pos, value.data() + colon, part);
      result = result * 60 + part;
      pos = colon + 1;
    }
  } else {
    std::from_chars(value.data(), value.data() + value.size(), result);
  }
  return sign * result;
}

// Returns the value of a scalar of `type` as Python's str() writes it, for
// mapping keys.
std::string KeyText(ScalarType type, const std::string& value) {
  switch (type) {
    case ScalarType::kNull:
      return "None";
    case ScalarType::kBool: {
      const std::string lower = AsciiLower(value);
      return lower == "yes" || lower == "true" || lower == "on" ? "True"
                                                                : "False";
    }
    case ScalarType::kInt: {
      const std::optional<int64_t> number = ConstructInt(value);
      return number ? std::to_string(*number) : value;
    }
    case ScalarType::kFloat:
      return FloatRepr(ConstructFloat(value));
    case ScalarType::kString:
      return value;
  }
  return value;
}

struct ParserDeleter {
  void operator()(yaml_parser_t* parser) const {
    yaml_parser_delete(parser);
    delete parser;
  }
};

struct EventDeleter {
  void operator()(yaml_event_t* event) const {
    yaml_event_delete(event);
    delete event;
  }
};

std::string Text(const yaml_char_t* text) {
  return text == nullptr ? "" : reinterpret_cast<const char*>(text);
}

// Returns PyYAML's message for a parse error of libyaml.
Error ParserError(const yaml_parser_t& parser, std::string_view name) {
  const auto mark = [name](const yaml_mark_t& mark) {
    return std::format("  in \"{}\", line {}, column {}", name, mark.line + 1,
                       mark.column + 1);
  };
  std::string message;
  if (parser.context != nullptr) {
    message = std::format("{}\n{}\n", parser.context,
                          mark(parser.context_mark));
  }
  message += std::format("{}\n{}",
                         parser.problem != nullptr ? parser.problem : "error",
                         mark(parser.problem_mark));
  return Error{.message = message};
}

// Builds the yyjson document from libyaml's events, like PyYAML's Composer
// and SafeConstructor.
class YamlBuilder {
 public:
  // With `resolve_types` false, every scalar is a string (BaseLoader).
  YamlBuilder(std::string_view name, bool resolve_types)
      : name_(name),
        resolve_types_(resolve_types),
        document_(NewJsonDocument()) {}

  YamlBuilder(const YamlBuilder&) = delete;
  YamlBuilder& operator=(const YamlBuilder&) = delete;

  Result<void> Add(const yaml_event_t& event);
  JsonDocument Finish();

 private:
  // A mapping or sequence being built.
  struct Container {
    yyjson_mut_val* value;
    std::string anchor;
    // Mappings only: the key waiting for its value.
    std::optional<std::string> key;
  };

  Result<yyjson_mut_val*> Scalar(const yaml_event_t& event,
                                 std::string* key_text);
  Result<void> Place(yyjson_mut_val* value, const std::string& key_text,
                     const std::string& anchor);

  std::string_view name_;
  bool resolve_types_;
  MutableJsonDocument document_;
  yyjson_mut_val* root_ = nullptr;
  int documents_ = 0;
  std::vector<Container> stack_;
  std::map<std::string, yyjson_mut_val*> anchors_;
};

Result<yyjson_mut_val*> YamlBuilder::Scalar(const yaml_event_t& event,
                                            std::string* key_text) {
  const std::string value(
      reinterpret_cast<const char*>(event.data.scalar.value),
      event.data.scalar.length);
  const std::string tag = Text(event.data.scalar.tag);
  ScalarType type = ScalarType::kString;
  if (!resolve_types_) {
    // BaseLoader: the value is the string, whatever the tag.
  } else if (tag.empty() || tag == "!") {
    if (event.data.scalar.plain_implicit) type = ResolvePlain(value);
  } else if (tag == std::string(kTagPrefix) + "null") {
    type = ScalarType::kNull;
  } else if (tag == std::string(kTagPrefix) + "bool") {
    type = ScalarType::kBool;
  } else if (tag == std::string(kTagPrefix) + "int") {
    type = ScalarType::kInt;
  } else if (tag == std::string(kTagPrefix) + "float") {
    type = ScalarType::kFloat;
  } else if (tag != std::string(kTagPrefix) + "str" &&
             tag != std::string(kTagPrefix) + "timestamp") {
    return Result<yyjson_mut_val*>(Error{
        .message = std::format("could not determine a constructor for the "
                               "tag '{}'",
                               tag)});
  }
  *key_text = KeyText(type, value);
  yyjson_mut_doc* doc = document_.get();
  switch (type) {
    case ScalarType::kNull:
      return Result<yyjson_mut_val*>(yyjson_mut_null(doc));
    case ScalarType::kBool:
      return Result<yyjson_mut_val*>(yyjson_mut_bool(doc, *key_text == "True"));
    case ScalarType::kInt:
      if (const std::optional<int64_t> number = ConstructInt(value)) {
        return Result<yyjson_mut_val*>(yyjson_mut_sint(doc, *number));
      }
      break;  // Too big for 64 bits: kept as a string.
    case ScalarType::kFloat:
      return Result<yyjson_mut_val*>(
          yyjson_mut_real(doc, ConstructFloat(value)));
    case ScalarType::kString:
      break;
  }
  return Result<yyjson_mut_val*>(
      yyjson_mut_strncpy(doc, value.data(), value.size()));
}

Result<void> YamlBuilder::Place(yyjson_mut_val* value,
                                const std::string& key_text,
                                const std::string& anchor) {
  if (!anchor.empty()) anchors_[anchor] = value;
  if (stack_.empty()) {
    root_ = value;
    return Result<void>();
  }
  Container& parent = stack_.back();
  if (yyjson_mut_is_arr(parent.value)) {
    yyjson_mut_arr_append(parent.value, value);
  } else if (!parent.key) {
    if (yyjson_mut_is_arr(value) || yyjson_mut_is_obj(value)) {
      return Result<void>(Error{
          .message = "found unhashable key while constructing a mapping"});
    }
    parent.key = key_text;
  } else {
    yyjson_mut_obj_put(parent.value,
                       yyjson_mut_strncpy(document_.get(), parent.key->data(),
                                          parent.key->size()),
                       value);
    parent.key.reset();
  }
  return Result<void>();
}

Result<void> YamlBuilder::Add(const yaml_event_t& event) {
  yyjson_mut_doc* doc = document_.get();
  switch (event.type) {
    case YAML_DOCUMENT_START_EVENT:
      if (++documents_ > 1) {
        return Result<void>(Error{
            .message = "expected a single document in the stream\n"
                       "but found another document"});
      }
      return Result<void>();
    case YAML_SCALAR_EVENT: {
      std::string key_text;
      Result<yyjson_mut_val*> value = Scalar(event, &key_text);
      if (!value.ok()) return Result<void>(value.error());
      return Place(value.value(), key_text,
                   Text(event.data.scalar.anchor));
    }
    case YAML_ALIAS_EVENT: {
      const auto anchor = anchors_.find(Text(event.data.alias.anchor));
      if (anchor == anchors_.end()) {
        return Result<void>(Error{.message = std::format(
            "found undefined alias '{}'", Text(event.data.alias.anchor))});
      }
      // A value can be in one place of a yyjson document only.
      yyjson_mut_val* copy = yyjson_mut_val_mut_copy(doc, anchor->second);
      const std::string key_text =
          yyjson_mut_is_str(copy) ? yyjson_mut_get_str(copy) : "";
      return Place(copy, key_text, "");
    }
    case YAML_SEQUENCE_START_EVENT:
    case YAML_MAPPING_START_EVENT: {
      const bool mapping = event.type == YAML_MAPPING_START_EVENT;
      yyjson_mut_val* value =
          mapping ? yyjson_mut_obj(doc) : yyjson_mut_arr(doc);
      const std::string anchor =
          Text(mapping ? event.data.mapping_start.anchor
                       : event.data.sequence_start.anchor);
      if (Result<void> placed = Place(value, "", ""); !placed.ok()) {
        return placed;
      }
      stack_.push_back({.value = value, .anchor = anchor, .key = {}});
      return Result<void>();
    }
    case YAML_SEQUENCE_END_EVENT:
    case YAML_MAPPING_END_EVENT:
      if (!stack_.back().anchor.empty()) {
        anchors_[stack_.back().anchor] = stack_.back().value;
      }
      stack_.pop_back();
      return Result<void>();
    default:
      return Result<void>();
  }
}

JsonDocument YamlBuilder::Finish() {
  yyjson_mut_doc_set_root(
      document_.get(),
      root_ != nullptr ? root_ : yyjson_mut_null(document_.get()));
  return JsonDocument(yyjson_mut_doc_imut_copy(document_.get(), nullptr));
}

// Python: the Loader classes; see SafeLoadYaml() and LoadYamlStrings().
Result<JsonDocument> LoadYaml(std::string_view text, std::string_view name,
                              bool resolve_types) {
  const std::unique_ptr<yaml_parser_t, ParserDeleter> parser(
      new yaml_parser_t);
  yaml_parser_initialize(parser.get());
  yaml_parser_set_input_string(
      parser.get(), reinterpret_cast<const unsigned char*>(text.data()),
      text.size());
  YamlBuilder builder(name, resolve_types);
  while (true) {
    const std::unique_ptr<yaml_event_t, EventDeleter> event(new yaml_event_t);
    if (!yaml_parser_parse(parser.get(), event.get())) {
      return Result<JsonDocument>(ParserError(*parser, name));
    }
    if (Result<void> added = builder.Add(*event); !added.ok()) {
      return Result<JsonDocument>(added.error());
    }
    if (event->type == YAML_STREAM_END_EVENT) break;
  }
  return Result<JsonDocument>(builder.Finish());
}

struct EmitterDeleter {
  void operator()(yaml_emitter_t* emitter) const {
    yaml_emitter_delete(emitter);
    delete emitter;
  }
};

// Collects the emitter's output in the std::string `data`.
int AppendOutput(void* data, unsigned char* buffer, size_t size) {
  static_cast<std::string*>(data)->append(reinterpret_cast<char*>(buffer),
                                          size);
  return 1;
}

yaml_char_t* Chars(const std::string& text) {
  return reinterpret_cast<yaml_char_t*>(const_cast<char*>(text.c_str()));
}

// Returns the text PyYAML's Representer gives a scalar, and whether it is a
// string (then it is quoted if it would read back as another type).
std::pair<std::string, bool> ScalarText(yyjson_mut_val* value) {
  switch (yyjson_mut_get_type(value)) {
    case YYJSON_TYPE_NULL:
      return {"null", false};
    case YYJSON_TYPE_BOOL:
      return {yyjson_mut_get_bool(value) ? "true" : "false", false};
    case YYJSON_TYPE_NUM: {
      if (yyjson_mut_is_int(value)) {
        return {yyjson_mut_is_uint(value)
                    ? std::to_string(yyjson_mut_get_uint(value))
                    : std::to_string(yyjson_mut_get_sint(value)),
                false};
      }
      const double real = yyjson_mut_get_real(value);
      if (std::isnan(real)) return {".nan", false};
      if (std::isinf(real)) return {real > 0 ? ".inf" : "-.inf", false};
      std::string text = AsciiLower(FloatRepr(real));
      // PyYAML: "1e+17" is not a valid float, so it writes "1.0e+17".
      if (text.find('.') == std::string::npos) {
        if (const size_t e = text.find('e'); e != std::string::npos) {
          text.insert(e, ".0");
        }
      }
      return {text, false};
    }
    default:
      return {JsonString(value), true};
  }
}

// Emits `value` and its items as libyaml events.
bool EmitNode(yaml_emitter_t* emitter, yyjson_mut_val* value) {
  yaml_event_t event;
  if (yyjson_mut_is_arr(value)) {
    yaml_sequence_start_event_initialize(&event, nullptr, nullptr, 1,
                                         YAML_BLOCK_SEQUENCE_STYLE);
    if (!yaml_emitter_emit(emitter, &event)) return false;
    yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(value);
    while (yyjson_mut_val* item = yyjson_mut_arr_iter_next(&iter)) {
      if (!EmitNode(emitter, item)) return false;
    }
    yaml_sequence_end_event_initialize(&event);
    return yaml_emitter_emit(emitter, &event) != 0;
  }
  if (yyjson_mut_is_obj(value)) {
    yaml_mapping_start_event_initialize(&event, nullptr, nullptr, 1,
                                        YAML_BLOCK_MAPPING_STYLE);
    if (!yaml_emitter_emit(emitter, &event)) return false;
    // Pairs of key and value, sorted by key as PyYAML's sort_keys does.
    std::vector<std::pair<yyjson_mut_val*, yyjson_mut_val*>> items;
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
    while (yyjson_mut_val* key = yyjson_mut_obj_iter_next(&iter)) {
      items.emplace_back(key, yyjson_mut_obj_iter_get_val(key));
    }
    const auto key_text = [](yyjson_mut_val* key) {
      return std::string_view(yyjson_mut_get_str(key), yyjson_mut_get_len(key));
    };
    std::ranges::stable_sort(items, [&key_text](const auto& a, const auto& b) {
      return key_text(a.first) < key_text(b.first);
    });
    for (const auto& [key, item] : items) {
      if (!EmitNode(emitter, key) || !EmitNode(emitter, item)) return false;
    }
    yaml_mapping_end_event_initialize(&event);
    return yaml_emitter_emit(emitter, &event) != 0;
  }
  const auto [text, is_string] = ScalarText(value);
  // Like PyYAML's Serializer: a string is written plain only if it reads
  // back as a string.
  const int plain_implicit =
      !is_string || ResolvePlain(text) == ScalarType::kString;
  yaml_scalar_event_initialize(&event, nullptr, nullptr, Chars(text),
                               static_cast<int>(text.size()), plain_implicit,
                               is_string, YAML_ANY_SCALAR_STYLE);
  return yaml_emitter_emit(emitter, &event) != 0;
}

}  // namespace

Result<JsonDocument> SafeLoadYaml(std::string_view text,
                                  std::string_view name) {
  return LoadYaml(text, name, /*resolve_types=*/true);
}

Result<JsonDocument> LoadYamlStrings(std::string_view text,
                                     std::string_view name) {
  return LoadYaml(text, name, /*resolve_types=*/false);
}

std::string DumpYaml(yyjson_mut_val* value, int width) {
  std::string out;
  const std::unique_ptr<yaml_emitter_t, EmitterDeleter> emitter(
      new yaml_emitter_t);
  yaml_emitter_initialize(emitter.get());
  yaml_emitter_set_output(emitter.get(), AppendOutput, &out);
  yaml_emitter_set_unicode(emitter.get(), 0);
  yaml_emitter_set_width(emitter.get(), width);
  yaml_emitter_set_indent(emitter.get(), 2);
  yaml_event_t event;
  yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
  bool ok = yaml_emitter_emit(emitter.get(), &event) != 0;
  yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 1);
  ok = ok && yaml_emitter_emit(emitter.get(), &event) != 0;
  ok = ok && EmitNode(emitter.get(), value);
  yaml_document_end_event_initialize(&event, 1);
  ok = ok && yaml_emitter_emit(emitter.get(), &event) != 0;
  yaml_stream_end_event_initialize(&event);
  ok = ok && yaml_emitter_emit(emitter.get(), &event) != 0;
  // The emitter fails only for documents built wrong.
  if (!ok) std::terminate();
  return out;
}

}  // namespace cc::util
