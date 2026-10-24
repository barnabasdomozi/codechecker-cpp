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
6 reports.

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
| `src/lib/stock.cpp:16:7` | clangsa | `cplusplus.NewDeleteLeaks` | HIGH | `cba55b2b2422c9a9c40010975a252f06` | unreviewed |
| `src/lib/stock.cpp:41:21` | clangsa | `core.DivideZero` | HIGH | `bee9daf03db494510ef6e2eee8d71eee` | unreviewed |
| `third_party/tinyjson/tinyjson.cpp:12:10` | clangsa | `core.NullDereference` | HIGH | `714113d912e48f1e1f285b91ef1fa4d4` | unreviewed |

- `analyze` exits 0. `parse` exits 2.
- The `item.h` report is listed twice with the same hash: once from the
  analysis of `src/lib/item.cpp` and once from `src/app/main.cpp`. `parse`
  prints it twice, each time under `Found 1 defect(s) in item.h`.

### With the skipfile

```sh
CodeChecker analyze compile_commands.json -i skipfile -o reports
```

`skipfile` contains `-*/third_party/*`. Result: the 5 reports above without
the `tinyjson.cpp` one.
