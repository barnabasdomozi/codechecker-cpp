// XML documents read into a tree with libxml2, with accessors that give the
// results of Python's xml.etree.ElementTree (libraries.md, "XML and plist").
// Nodes are libxml2's xmlNode; only element nodes are passed around.

#ifndef CC_UTIL_XML_H_
#define CC_UTIL_XML_H_

#include <libxml/tree.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace cc::util {

// Frees libxml2 documents.
struct XmlDocumentDeleter {
  void operator()(xmlDoc* document) const { xmlFreeDoc(document); }
};

// An owned XML document.
using XmlDocument = std::unique_ptr<xmlDoc, XmlDocumentDeleter>;

// Parses `xml`. The error message is libxml2's first error.
Result<XmlDocument> ParseXml(std::string_view xml);

// Returns the root element of `document` (Python: tree.getroot()).
xmlNode* XmlRoot(const XmlDocument& document);

// Returns the tag of `element`.
std::string_view XmlTag(const xmlNode* element);

// Returns the child elements of `element` (Python: list(element)).
std::vector<xmlNode*> XmlChildren(const xmlNode* element);

// Returns the first child element of `element` with the tag `tag`, or null
// (Python: element.find(tag)).
xmlNode* XmlFindChild(const xmlNode* element, std::string_view tag);

// Returns the text before the first child element of `element`, or nullopt
// if there is none (Python: element.text).
std::optional<std::string> XmlText(const xmlNode* element);

// Returns the attribute `name` of `element`, or nullopt (Python:
// element.attrib.get(name)).
std::optional<std::string> XmlAttribute(const xmlNode* element,
                                        std::string_view name);

}  // namespace cc::util

#endif  // CC_UTIL_XML_H_
