// Does not compile: missing semicolon.
int broken(int a) {
  int z = 0
  return a / z;
}
