#include "lib/util.h"
#include <stdio.h>

int divide(int a) {
  int z = get_zero();
  return a / z; /* only found with CTU */
}

int local_div(int a) {
  int z = 0;
  return a / z;
}

void deref(void) {
  int *p = get_null();
  *p = 42; /* only found with CTU */
}

int main(void) {
  int x;
  int unused = 5;
  printf("%d\n", local_div(10));
  printf("%d\n", divide(10));
  deref();
  return x;
}
