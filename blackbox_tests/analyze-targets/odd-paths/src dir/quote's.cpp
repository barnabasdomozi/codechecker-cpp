#include "shared header.h"

// GREETING is defined on the command line as a string with a space.
const char *greeting() {
  const char *text = GREETING;
  int unused_length = 0;
  return text;
}
