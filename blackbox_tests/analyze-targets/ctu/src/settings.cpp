#include "api.h"

void find_setting(Settings &settings, char key, int *&out) {
  out = nullptr;
  if (key == 'v')
    out = &settings.verbosity;
}

int default_divisor() { return 0; }

void Counter::reset() {
  hits = 0;
  slot = nullptr;
}
