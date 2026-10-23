# Demo project

The examples in these pages use this small project. It is built with `make`
and contains bugs that the default checkers find.

```
~/demo
├── Makefile
├── main.c
├── style.cpp
└── lib
    ├── util.c
    └── util.h
```

`lib/util.h`
```c
#ifndef UTIL_H
#define UTIL_H
int get_zero(void);
int *get_null(void);
#endif
```

`lib/util.c`
```c
#include "util.h"
#include <stddef.h>
int get_zero(void) { return 0; }
int *get_null(void) { return NULL; }
```

`main.c`
```c
#include "lib/util.h"
#include <stdio.h>

int divide(int a) {
  int z = get_zero();
  return a / z; /* only found with CTU */
}

int local_div(int a) {
  int z = 0;
  return a / z;
}

void deref(void) {
  int *p = get_null();
  *p = 42; /* only found with CTU */
}

int main(void) {
  int x;
  int unused = 5;
  printf("%d\n", local_div(10));
  printf("%d\n", divide(10));
  deref();
  return x;
}
```

`style.cpp`
```cpp
#include <cstddef>

int *make_ptr() {
  int *p = 0; // clang-tidy: modernize-use-nullptr fixit
  return p;
}

int suppressed(int a) {
  int z = 0;
  // codechecker_suppress [core.DivideZero] intentional for the demo
  return a / z;
}

int false_pos(int a) {
  int z = 0;
  // codechecker_false_positive [all] demo comment
  return a / z;
}
```

`Makefile`
```make
all: app style.o
app: main.o lib/util.o
	gcc -o app main.o lib/util.o
main.o: main.c
	gcc -c main.c -o main.o
lib/util.o: lib/util.c
	gcc -c lib/util.c -o lib/util.o
style.o: style.cpp
	g++ -std=c++17 -c style.cpp -o style.o
clean:
	rm -f app *.o lib/*.o
```

## What the default analysis finds

`CodeChecker check -b make` reports three issues in `main.c`:

| Location | Checker | Severity |
|---|---|---|
| `main.c:11:12` | `core.DivideZero` | HIGH |
| `main.c:21:7` | `clang-diagnostic-unused-variable` | MEDIUM |
| `main.c:25:10` | `clang-diagnostic-uninitialized` | HIGH |

The two divisions by zero in `style.cpp` are hidden by their source-code
comments. `main.c:6` is only found with CTU analysis.

## Second version

[local-diff.md](local-diff.md), [cmd-diff.md](cmd-diff.md) and
[store.md](store.md) also use a changed copy, `~/demo2`:

- `main.c:21` changes from `int unused = 5;` to `int *q = 0; *q = 1;`. This
  removes the unused-variable report and adds a `core.NullDereference`.
- `style.cpp:4` gets a new division by zero:
  `int *p = 0; int t = 0; (void)(1 / t);`.
