// A bug inside a macro expansion. The plist keeps the expanded macro.
#define SAFE_DIV(a, b) ((a) / (b))
#define NO_DIVISOR 0

int macro_division(int a) { return SAFE_DIV(a, NO_DIVISOR); }
