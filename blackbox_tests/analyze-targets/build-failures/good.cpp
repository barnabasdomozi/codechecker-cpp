// Compiles and is analyzed. Its bug is reported even though other files
// fail.
int good(int a) {
  int z = 0;
  return a / z;
}
