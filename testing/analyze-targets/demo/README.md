# `demo`

The demo project of the docs. The examples and expected outputs in
[docs/usage/](../../../docs/usage/README.md) were produced with it, so tests
that are copied from the docs use this project.

| Directory | Docs name | Sources |
|---|---|---|
| `v1/` | `~/demo` | [demo-project.md](../../../docs/usage/demo-project.md) |
| `v2/` | `~/demo2` | the "Second version" section of the same page |

Copy `v1/` to a directory named `demo` and `v2/` to `demo2`, so that paths
and run names look like the ones in the docs.

## Build

```sh
make
```

Mostly C (`main.c`, `lib/util.c`), plus one C++ file (`style.cpp`).

## Expected results

The full tables, with report hashes and review statuses, are in
[reference/demo-project.md](../../../docs/reference/demo-project.md), and the
black-box suite checks them. In short (Clang SA, Clang-Tidy, GCC and
Cppcheck 2.17.1):

- `v1`: 9 reports. `parse` shows 6 of them (the five `main.c` reports and
  Cppcheck's `style.cpp:11`) and exits 2; three are false positives hidden
  by source-code comments.
- `v2`: 12 reports. Compared with `v1`: `249a1217...` (unused variable) is
  resolved; `19a15bf4...` and `b8956890...` (Clang SA), `dc01f098...` and
  `0983180e...` (Cppcheck) are new.

`main.c:6` and `main.c:16` are only found with CTU
([ctu-analysis.md](../../../docs/reference/ctu-analysis.md)).
