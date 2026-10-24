// Input for running Clang-Tidy and the GCC analyzer directly, without
// CodeChecker. Their output is converted with report-converter.
#include <cstdlib>

int divide(int a) {
  int zero = 0;
  return a / zero;
}

void twice() {
  void *block = std::malloc(8);
  std::free(block);
  std::free(block);
}
