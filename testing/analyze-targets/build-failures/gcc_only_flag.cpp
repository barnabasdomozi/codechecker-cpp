// Compiles with GCC. The analysis fails because Clang rejects the option
// in the Makefile.
int gcc_only(int a) {
  int z = 0;
  return a / z;
}
