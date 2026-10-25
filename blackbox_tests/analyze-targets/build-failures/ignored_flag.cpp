// Compiles with GCC. The option in the Makefile is removed by CodeChecker,
// so this file is analyzed and its bug is reported.
int ignored(int a) {
  int *p = nullptr;
  if (a > 0)
    return *p;
  return a;
}
