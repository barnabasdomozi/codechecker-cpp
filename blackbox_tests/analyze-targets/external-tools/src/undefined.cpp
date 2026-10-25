// UndefinedBehaviorSanitizer: signed integer overflow and an invalid shift.
#include <climits>
#include <cstdio>

int main(int argc, char **) {
  int big = INT_MAX;
  int sum = big + argc;
  int shift = 1 << (argc + 31);
  std::printf("%d %d\n", sum, shift);
  return 0;
}
