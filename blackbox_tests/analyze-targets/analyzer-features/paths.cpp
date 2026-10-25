// A bug at the end of a long path: a loop, several branches and a call
// that changes the pointer through a parameter. Useful for
// 'parse --print-steps' and bug path length filters.

static void adjust(int **target, int mode, int *storage) {
  if (mode == 1)
    *target = nullptr;
  else if (mode == 2)
    *target = storage + 1;
}

int long_path(int mode, int count) {
  int storage[2] = {0, 0};
  int *target = storage;
  int total = 0;
  for (int i = 0; i < count; ++i)
    total += i;
  adjust(&target, mode, storage);
  if (total > 10)
    target = storage;
  return *target;
}
