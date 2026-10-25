#include "tinyjson.h"

#include <cstring>

namespace tinyjson {

const char *quote(const char *text) {
  static char buffer[64];
  char *out = nullptr;
  if (std::strlen(text) < sizeof(buffer) - 2)
    out = buffer;
  out[0] = '"';
  std::strcpy(out + 1, text);
  std::strcat(out, "\"");
  return out;
}

} // namespace tinyjson
