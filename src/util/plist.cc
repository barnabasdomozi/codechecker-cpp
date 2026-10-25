#include "util/plist.h"

#include <libxml/xmlerror.h>
#include <libxml/xmlreader.h>
#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "util/json.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"

namespace cc::util {
namespace {

struct ReaderDeleter {
  void operator()(xmlTextReader* reader) const { xmlFreeTextReader(reader); }
};

// Keeps the first error that libxml2 reports, instead of printing it.
void KeepFirstError(void* user_data, const xmlError* error) {
  auto* first = static_cast<std::optional<std::string>*>(user_data);
  if (*first || error == nullptr) return;
  std::string message = error->message != nullptr ? error->message : "";
  while (!message.empty() && message.back() == '\n') message.pop_back();
  *first = std::format("{}, line {}, column {}", message, error->line,
                       error->int2);
}

// Builds the value tree from the elements of a plist, like plistlib's
// _PlistParser.
class PlistBuilder {
 public:
  PlistBuilder() : document_(NewJsonDocument()) {}

  PlistBuilder(const PlistBuilder&) = delete;
  PlistBuilder& operator=(const PlistBuilder&) = delete;

  void AddData(std::string_view data) { data_ += data; }
  Result<void> Begin(std::string_view element);
  Result<void> End(std::string_view element);
  // Returns the document; the root is null if the plist has no value.
  JsonDocument Finish();

 private:
  Result<void> Add(yyjson_mut_val* value);

  MutableJsonDocument document_;
  yyjson_mut_val* root_ = nullptr;
  std::vector<yyjson_mut_val*> stack_;
  std::optional<std::string> current_key_;
  std::string data_;
};

Result<void> PlistBuilder::Begin(std::string_view element) {
  data_.clear();
  if (element == "dict" || element == "array") {
    yyjson_mut_val* container = element == "dict"
                                    ? yyjson_mut_obj(document_.get())
                                    : yyjson_mut_arr(document_.get());
    if (Result<void> added = Add(container); !added.ok()) return added;
    stack_.push_back(container);
  }
  return Result<void>();
}

Result<void> PlistBuilder::End(std::string_view element) {
  yyjson_mut_doc* doc = document_.get();
  if (element == "dict" || element == "array") {
    if (element == "dict" && current_key_) {
      return Result<void>(Error{.message = std::format(
                              "missing value for key '{}'", *current_key_)});
    }
    if (!stack_.empty()) stack_.pop_back();
    return Result<void>();
  }
  if (element == "key") {
    if (current_key_ || stack_.empty() || !yyjson_mut_is_obj(stack_.back())) {
      return Result<void>(Error{.message = "unexpected key"});
    }
    current_key_ = std::move(data_);
    data_.clear();
    return Result<void>();
  }
  yyjson_mut_val* value = nullptr;
  if (element == "true" || element == "false") {
    value = yyjson_mut_bool(doc, element == "true");
  } else if (element == "integer") {
    const std::string_view raw = StripAsciiWhitespace(data_);
    std::optional<int64_t> number;
    if (raw.starts_with("0x") || raw.starts_with("0X")) {
      const std::string_view digits = raw.substr(2);
      int64_t hex = 0;
      const std::from_chars_result result = std::from_chars(
          digits.data(), digits.data() + digits.size(), hex, 16);
      if (!digits.empty() && result.ec == std::errc() &&
          result.ptr == digits.data() + digits.size()) {
        number = hex;
      }
    } else {
      number = ParseInt(raw);
    }
    if (!number) {
      return Result<void>(Error{.message = std::format(
          "invalid literal for int() with base 10: {}", Repr(data_))});
    }
    value = yyjson_mut_sint(doc, *number);
  } else if (element == "real") {
    const std::string_view text = StripAsciiWhitespace(data_);
    double number = 0;
    const std::from_chars_result result =
        std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || result.ec != std::errc() ||
        result.ptr != text.data() + text.size()) {
      return Result<void>(Error{.message = std::format(
          "could not convert string to float: {}", Repr(data_))});
    }
    value = yyjson_mut_real(doc, number);
  } else if (element == "string" || element == "date" || element == "data") {
    value = yyjson_mut_strncpy(doc, data_.data(), data_.size());
  } else {
    return Result<void>();  // plistlib ignores other elements.
  }
  data_.clear();
  return Add(value);
}

Result<void> PlistBuilder::Add(yyjson_mut_val* value) {
  if (current_key_) {
    if (stack_.empty() || !yyjson_mut_is_obj(stack_.back())) {
      return Result<void>(Error{.message = "unexpected element"});
    }
    yyjson_mut_obj_put(
        stack_.back(),
        yyjson_mut_strncpy(document_.get(), current_key_->data(),
                           current_key_->size()),
        value);
    current_key_.reset();
  } else if (stack_.empty()) {
    root_ = value;
  } else {
    if (!yyjson_mut_is_arr(stack_.back())) {
      return Result<void>(Error{.message = "unexpected element"});
    }
    yyjson_mut_arr_append(stack_.back(), value);
  }
  return Result<void>();
}

JsonDocument PlistBuilder::Finish() {
  yyjson_mut_doc_set_root(
      document_.get(),
      root_ != nullptr ? root_ : yyjson_mut_null(document_.get()));
  return JsonDocument(yyjson_mut_doc_imut_copy(document_.get(), nullptr));
}

// Appends `text` as plistlib's _escape() writes it, or fails like it.
Result<void> AppendEscaped(std::string_view text, std::string* out) {
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 && c != '\t' && c != '\n' && c != '\r') {
      return Result<void>(Error{
          .message = "strings can't contain control characters; use bytes "
                     "instead"});
    }
  }
  for (size_t i = 0; i < text.size(); ++i) {
    switch (text[i]) {
      case '\r':
        *out += '\n';
        if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        break;
      case '&':
        *out += "&amp;";
        break;
      case '<':
        *out += "&lt;";
        break;
      case '>':
        *out += "&gt;";
        break;
      default:
        *out += text[i];
    }
  }
  return Result<void>();
}

std::string_view StringOf(yyjson_mut_val* value) {
  return std::string_view(yyjson_mut_get_str(value), yyjson_mut_get_len(value));
}

// Appends `value` at indent `level` (Python: _PlistWriter.write_value()).
Result<void> AppendValue(yyjson_mut_val* value, int level, std::string* out) {
  const std::string indent(level, '\t');
  switch (yyjson_mut_get_type(value)) {
    case YYJSON_TYPE_STR:
      *out += indent + "<string>";
      if (Result<void> escaped = AppendEscaped(StringOf(value), out);
          !escaped.ok()) {
        return escaped;
      }
      *out += "</string>\n";
      return Result<void>();
    case YYJSON_TYPE_BOOL:
      *out += indent +
              (yyjson_mut_get_bool(value) ? "<true/>\n" : "<false/>\n");
      return Result<void>();
    case YYJSON_TYPE_NUM:
      if (yyjson_mut_get_subtype(value) == YYJSON_SUBTYPE_UINT) {
        *out += std::format("{}<integer>{}</integer>\n", indent,
                            yyjson_mut_get_uint(value));
      } else if (yyjson_mut_get_subtype(value) == YYJSON_SUBTYPE_SINT) {
        *out += std::format("{}<integer>{}</integer>\n", indent,
                            yyjson_mut_get_sint(value));
      } else {
        *out += std::format("{}<real>{}</real>\n", indent,
                            FloatRepr(yyjson_mut_get_real(value)));
      }
      return Result<void>();
    case YYJSON_TYPE_ARR: {
      if (yyjson_mut_arr_size(value) == 0) {
        *out += indent + "<array/>\n";
        return Result<void>();
      }
      *out += indent + "<array>\n";
      yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(value);
      while (yyjson_mut_val* item = yyjson_mut_arr_iter_next(&iter)) {
        if (Result<void> added = AppendValue(item, level + 1, out);
            !added.ok()) {
          return added;
        }
      }
      *out += indent + "</array>\n";
      return Result<void>();
    }
    case YYJSON_TYPE_OBJ: {
      if (yyjson_mut_obj_size(value) == 0) {
        *out += indent + "<dict/>\n";
        return Result<void>();
      }
      std::vector<yyjson_mut_val*> keys;
      yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
      while (yyjson_mut_val* key = yyjson_mut_obj_iter_next(&iter)) {
        keys.push_back(key);
      }
      std::ranges::stable_sort(keys, {}, StringOf);
      *out += indent + "<dict>\n";
      for (yyjson_mut_val* key : keys) {
        *out += indent + "\t<key>";
        if (Result<void> escaped = AppendEscaped(StringOf(key), out);
            !escaped.ok()) {
          return escaped;
        }
        *out += "</key>\n";
        if (Result<void> added =
                AppendValue(yyjson_mut_obj_iter_get_val(key), level + 1, out);
            !added.ok()) {
          return added;
        }
      }
      *out += indent + "</dict>\n";
      return Result<void>();
    }
    default:
      return Result<void>(
          Error{.message = "unsupported type: <class 'NoneType'>"});
  }
}

}  // namespace

Result<JsonDocument> ParsePlist(std::string_view xml,
                                std::string_view file_name) {
  // lxml's defaults: no network, no external DTD, no huge documents.
  const std::unique_ptr<xmlTextReader, ReaderDeleter> reader(
      xmlReaderForMemory(xml.data(), static_cast<int>(xml.size()), nullptr,
                         nullptr, XML_PARSE_NONET));
  if (reader == nullptr) {
    return Result<JsonDocument>(Error{.message = "cannot create XML reader"});
  }
  std::optional<std::string> syntax_error;
  xmlTextReaderSetStructuredErrorHandler(reader.get(), KeepFirstError,
                                         &syntax_error);
  PlistBuilder builder;
  int status = 0;
  while ((status = xmlTextReaderRead(reader.get())) == 1) {
    const xmlChar* raw_name = xmlTextReaderConstName(reader.get());
    const std::string_view name =
        raw_name != nullptr ? reinterpret_cast<const char*>(raw_name) : "";
    Result<void> step;
    switch (xmlTextReaderNodeType(reader.get())) {
      case XML_READER_TYPE_ELEMENT:
        step = builder.Begin(name);
        if (step.ok() && xmlTextReaderIsEmptyElement(reader.get()) == 1) {
          step = builder.End(name);
        }
        break;
      case XML_READER_TYPE_END_ELEMENT:
        step = builder.End(name);
        break;
      case XML_READER_TYPE_TEXT:
      case XML_READER_TYPE_CDATA:
      case XML_READER_TYPE_WHITESPACE:
      case XML_READER_TYPE_SIGNIFICANT_WHITESPACE: {
        const xmlChar* value = xmlTextReaderConstValue(reader.get());
        if (value != nullptr) {
          builder.AddData(reinterpret_cast<const char*>(value));
        }
        break;
      }
      default:
        break;
    }
    if (!step.ok()) return Result<JsonDocument>(step.error());
  }
  if (status < 0 || syntax_error) {
    const std::string message =
        syntax_error.value_or("Document is empty, line 1, column 1");
    // lxml's XMLSyntaxError ends with "(<file>, line <n>)".
    const size_t line_start = message.rfind(", line ");
    const std::string line =
        message.substr(line_start + 7, message.find(',', line_start + 7) -
                                           line_start - 7);
    return Result<JsonDocument>(
        Error{.message = std::format("{} ({}, line {})", message,
                                     pypath::Basename(file_name), line)});
  }
  return Result<JsonDocument>(builder.Finish());
}

Result<std::string> DumpPlist(yyjson_mut_val* value) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
      "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\">\n";
  if (Result<void> added = AppendValue(value, 0, &out); !added.ok()) {
    return Result<std::string>(added.error());
  }
  out += "</plist>\n";
  return Result<std::string>(std::move(out));
}

}  // namespace cc::util
