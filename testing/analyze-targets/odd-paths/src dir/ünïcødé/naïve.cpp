#include "shared header.h"

// Ein Kommentar mit Umlauten: Größe, Übermaß. 日本語のコメント.
int naive_ratio(int a, int b) {
  int zero = 0;
  if (b > 100)
    return a / zero;
  return a / b;
}
