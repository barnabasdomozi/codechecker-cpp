#include "buffer.h" // found through -I../core from flags.rsp

#include <cstdio>

int main() {
  core::Buffer buffer(16);
  int divisor = 0;
#if TOOL_LEVEL > 1
  // TOOL_LEVEL comes from flags.rsp.
  std::printf("%d\n", 100 / divisor);
#endif
  std::printf("%d\n", core::encode(buffer, 7));
  return 0;
}
