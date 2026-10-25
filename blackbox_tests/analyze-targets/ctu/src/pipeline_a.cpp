#include "api.h"

// Passes nullptr down the chain for positive values. The null pointer is
// only dereferenced two calls later, in pipeline_c.cpp.
int stage_a(int value) {
  int *input = value > 0 ? nullptr : &value;
  return stage_b(input);
}
