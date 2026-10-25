// LeakSanitizer: memory that is never freed.
#include <cstdio>

int main() {
  int *lost = new int[16];
  lost[0] = 1;
  std::printf("%d\n", lost[0]);
  lost = nullptr;
  return 0;
}
