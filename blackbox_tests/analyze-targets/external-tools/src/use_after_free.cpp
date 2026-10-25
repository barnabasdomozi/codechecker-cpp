// AddressSanitizer: heap-use-after-free.
int main() {
  int *value = new int(7);
  delete value;
  return *value;
}
