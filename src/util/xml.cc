#include "util/xml.h"

#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::util {
namespace {

struct ParserContextDeleter {
  void operator()(xmlParserCtxt* context) const { xmlFreeParserCtxt(context); }
};

// Keeps the first error that libxml2 reports, instead of printing it.
void KeepFirstError(void* user_data, const xmlError* error) {
  auto* first = static_cast<std::optional<std::string>*>(user_data);
  if (*first || error == nullptr || error->message == nullptr) return;
  std::string message = error->message;
  while (!message.empty() && message.back() == '\n') message.pop_back();
  *first = message;
}

const char* Chars(const xmlChar* text) {
  return reinterpret_cast<const char*>(text);
}

}  // namespace

Result<XmlDocument> ParseXml(std::string_view xml) {
  const std::unique_ptr<xmlParserCtxt, ParserContextDeleter> context(
      xmlNewParserCtxt());
  std::optional<std::string> error;
  xmlCtxtSetErrorHandler(context.get(), KeepFirstError, &error);
  XmlDocument document(xmlCtxtReadMemory(context.get(), xml.data(),
                                         static_cast<int>(xml.size()),
                                         nullptr, nullptr, XML_PARSE_NONET));
  if (document == nullptr || XmlRoot(document) == nullptr) {
    return Result<XmlDocument>(
        Error{.message = error.value_or("no element found")});
  }
  return Result<XmlDocument>(std::move(document));
}

xmlNode* XmlRoot(const XmlDocument& document) {
  return xmlDocGetRootElement(document.get());
}

std::string_view XmlTag(const xmlNode* element) {
  return Chars(element->name);
}

std::vector<xmlNode*> XmlChildren(const xmlNode* element) {
  std::vector<xmlNode*> children;
  for (xmlNode* child = element->children; child != nullptr;
       child = child->next) {
    if (child->type == XML_ELEMENT_NODE) children.push_back(child);
  }
  return children;
}

xmlNode* XmlFindChild(const xmlNode* element, std::string_view tag) {
  for (xmlNode* child : XmlChildren(element)) {
    if (XmlTag(child) == tag) return child;
  }
  return nullptr;
}

std::optional<std::string> XmlText(const xmlNode* element) {
  std::optional<std::string> text;
  for (const xmlNode* child = element->children; child != nullptr;
       child = child->next) {
    if (child->type == XML_ELEMENT_NODE) break;
    if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) {
      text = text.value_or("") + Chars(child->content);
    }
  }
  return text;
}

std::optional<std::string> XmlAttribute(const xmlNode* element,
                                        std::string_view name) {
  const std::string terminated(name);
  xmlChar* value = xmlGetProp(
      element, reinterpret_cast<const xmlChar*>(terminated.c_str()));
  if (value == nullptr) return std::nullopt;
  std::string result = Chars(value);
  xmlFree(value);
  return result;
}

std::optional<uint32_t> HtmlEntityCodePoint(std::string_view name) {
  const std::string text(name);
  const htmlEntityDesc* entity =
      htmlEntityLookup(reinterpret_cast<const xmlChar*>(text.c_str()));
  if (entity == nullptr) return std::nullopt;
  return entity->value;
}

}  // namespace cc::util
