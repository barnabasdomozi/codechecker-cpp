// AddressSanitizer: heap-buffer-overflow.
int main(int argc, char **) {
  int *values = new int[4];
  int result = values[argc + 3];
  delete[] values;
  return result;
}
