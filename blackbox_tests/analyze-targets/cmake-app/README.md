# `cmake-app`

A CMake project: a static library (`src/lib/`) with public headers
(`include/`), an executable (`src/app/`) and a third-party library
(`third_party/tinyjson/`).

Used for:

- logging a CMake build, and analyzing the `compile_commands.json` that
  CMake writes itself ([log.md](../../../docs/usage/log.md),
  [analyze.md](../../../docs/usage/analyze.md));
- a bug in a header that two source files reach, so the same report comes
  from two analyses;
- skipping third-party code with a skipfile
  ([skip-and-file-filters.md](../../../docs/usage/skip-and-file-filters.md)).

## Build

Configure first, **outside** `CodeChecker log`, then log only the build:

```sh
cmake -S . -B build
CodeChecker log -b "cmake --build build" -o compile_commands.json
```

CMake also writes `build/compile_commands.json`
(`CMAKE_EXPORT_COMPILE_COMMANDS`). Analyzing that file gives the same
14 reports.

If the configure step is logged too (`log -b "cmake -S . -B build && cmake
--build build"`), the log also contains CMake's compiler checks
(`CMakeCXXCompilerId.cpp`, `CMakeCXXCompilerABI.cpp` in temporary folders
that CMake deletes). `analyze` then fails on them and exits 3, with
`CMakeCXXCompilerABI.cpp_*_compile_error.zip` files in `reports/failed/`.
This is the expected behavior and can be tested on purpose.

## Expected results

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `include/inventory/item.h:18:24` | clangsa | `core.DivideZero` | HIGH | `97348e08f349140f840bbc86a372fe82` | unreviewed |
| `include/inventory/item.h:18:24` | clangsa | `core.DivideZero` | HIGH | `97348e08f349140f840bbc86a372fe82` | unreviewed |
| `src/app/main.cpp:16:7` | clang-tidy | `clang-diagnostic-sometimes-uninitialized` | MEDIUM | `39a3b9fe0f3bf54a7f3a1efde799949a` | unreviewed |
| `src/app/main.cpp:18:65` | cppcheck | `cppcheck-uninitvar` | HIGH | `a807d592bdc1e7bd25241fb2040f1879` | unreviewed |
| `src/lib/stock.cpp:6:18` | cppcheck | `cppcheck-noCopyConstructor` | MEDIUM | `fe7913fe9c2d9aa96dfa26f9ae54b152` | unreviewed |
| `src/lib/stock.cpp:6:18` | cppcheck | `cppcheck-noOperatorEq` | MEDIUM | `b4a7e9289b0d729b60e6bcb8abb69c6e` | unreviewed |
| `src/lib/stock.cpp:15:18` | cppcheck | `cppcheck-uninitdata` | HIGH | `a18c30ea5130fafe12d2c8f70a74c4d8` | unreviewed |
| `src/lib/stock.cpp:16:7` | clangsa | `cplusplus.NewDeleteLeaks` | HIGH | `cba55b2b2422c9a9c40010975a252f06` | unreviewed |
| `src/lib/stock.cpp:41:21` | clangsa | `core.DivideZero` | HIGH | `bee9daf03db494510ef6e2eee8d71eee` | unreviewed |
| `src/lib/stock.cpp:41:21` | cppcheck | `cppcheck-zerodiv` | HIGH | `34845d2b9d9b61dc4e17632722cd1162` | unreviewed |
| `third_party/tinyjson/tinyjson.cpp:12:3` | cppcheck | `cppcheck-nullPointer` | HIGH | `a46442571196a5c82e047aaeb5b41146` | unreviewed |
| `third_party/tinyjson/tinyjson.cpp:12:10` | clangsa | `core.NullDereference` | HIGH | `714113d912e48f1e1f285b91ef1fa4d4` | unreviewed |
| `third_party/tinyjson/tinyjson.cpp:13:19` | cppcheck | `cppcheck-nullPointerArithmetic` | HIGH | `9b44b2ada6a66965904f837e8987dc10` | unreviewed |
| `third_party/tinyjson/tinyjson.cpp:14:15` | cppcheck | `cppcheck-nullPointer` | HIGH | `3cac254851e90f4b02f0d5f2e2b81d53` | unreviewed |

- `analyze` exits 0. `parse` exits 2.
- The `item.h` report is listed twice with the same hash: once from the
  analysis of `src/lib/item.cpp` and once from `src/app/main.cpp`. `parse`
  prints it twice, each time under `Found 1 defect(s) in item.h`.

### With the skipfile

```sh
CodeChecker analyze compile_commands.json -i skipfile -o reports
```

`skipfile` contains `-*/third_party/*`. Result: the 10 reports above
without the four `tinyjson.cpp` ones (one from Clang SA, three from
Cppcheck).
