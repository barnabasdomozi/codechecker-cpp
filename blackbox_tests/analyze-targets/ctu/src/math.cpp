#include "api.h"

// Bug only visible with CTU: default_divisor() is defined in settings.cpp.
int scale(int value) { return value / default_divisor(); }

// Bug visible without CTU too.
int local_scale(int value) {
  int divisor = 0;
  return value / divisor;
}
