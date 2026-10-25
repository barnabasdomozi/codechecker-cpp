// Does not compile: the header does not exist.
#include "does_not_exist.h"

int missing(int a) { return a / DOES_NOT_EXIST_DIVISOR; }
