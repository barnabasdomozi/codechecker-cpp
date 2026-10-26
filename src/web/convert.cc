#include "web/convert.h"

#include <optional>
#include <string>
#include <string_view>

#include "util/base64.h"
#include "util/unicode.h"

namespace cc::web {

std::optional<std::string> FromBase64(std::string_view text) {
  const std::optional<std::string> data = util::Base64Decode(text);
  if (!data) return std::nullopt;
  return util::DecodeUtf8Ignore(*data);
}

}  // namespace cc::web
