#include <cstddef>

int *make_ptr() {
  int *p = 0; // clang-tidy: modernize-use-nullptr fixit
  return p;
}

int suppressed(int a) {
  int z = 0;
  // codechecker_suppress [core.DivideZero] intentional for the demo
  return a / z;
}

int false_pos(int a) {
  int z = 0;
  // codechecker_false_positive [all] demo comment
  return a / z;
}
