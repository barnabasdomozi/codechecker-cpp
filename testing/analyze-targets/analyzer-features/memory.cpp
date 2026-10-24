// Memory bugs found by Clang SA (default profile) and the GCC analyzer.
#include <cstdlib>

struct Node {
  int value;
};

int use_after_delete() {
  Node *node = new Node{1};
  delete node;
  return node->value;
}

void mismatched_delete() {
  int *values = new int[8];
  delete values;
}

void double_free() {
  void *block = std::malloc(16);
  std::free(block);
  std::free(block);
}

int leak(int n) {
  int *data = new int[4];
  if (n > 0)
    return n;
  delete[] data;
  return 0;
}
