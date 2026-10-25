#include "api.h"

#include <cstdio>

int color_demo() {
  // Bug only visible with CTU: find_setting() sets 'color' to nullptr
  // for unknown keys (settings.cpp).
  Settings settings;
  int fallback = 0;
  int *color = &fallback;
  find_setting(settings, 'c', color);
  return *color;
}

int counter_demo() {
  // Bug only visible with CTU: Counter::reset() clears 'slot'.
  Counter counter;
  int value = 5;
  counter.slot = &value;
  counter.reset();
  return *counter.slot;
}

int main() {
  std::printf("%d\n", scale(10) + local_scale(10) + stage_a(1));
  return 0;
}
