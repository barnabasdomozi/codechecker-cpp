# `external-tools`

Programs and sources whose bugs are found by tools other than CodeChecker:
sanitizers at run time, and Clang-Tidy and the GCC analyzer run directly.
Their output is the input of `report-converter`.

Used for: [report-converter.md](../../../docs/usage/report-converter.md),
and chains like `report-converter` → `parse` / `store`.

| File | Tool | Bug |
|---|---|---|
| `src/heap_overflow.cpp` | AddressSanitizer | heap buffer overflow |
| `src/use_after_free.cpp` | AddressSanitizer | heap use after free |
| `src/undefined.cpp` | UndefinedBehaviorSanitizer | signed integer overflow, shift too large |
| `src/leak.cpp` | LeakSanitizer | memory leak |
| `src/analyzer_input.cpp` | Clang-Tidy, GCC `-fanalyzer` | division by zero, double free |

## Producing the tool output

```sh
make
```

This is not a build to log with `CodeChecker log`. It compiles each program
with its sanitizer, runs it, and saves the output in `out/`:

| File | Made by |
|---|---|
| `out/heap_overflow.asan.out`, `out/use_after_free.asan.out` | `make asan` |
| `out/undefined.ubsan.out` | `make ubsan` |
| `out/leak.lsan.out` | `make lsan` |
| `out/clang-tidy.out`, `out/clang-tidy-fixes.yaml` | `make clang-tidy` |
| `out/analyzer_input.cpp.sarif` | `make gcc` |

The sanitized programs fail on purpose. `make` ignores their exit codes and
exits 0.

## Expected results

| Conversion | Location | Analyzer | Checker | Severity | Report hash |
|---|---|---|---|---|---|
| `-t asan` (both `.asan.out` files) | `src/heap_overflow.cpp:4:16` | asan | `AddressSanitizer.generic-error` | HIGH | `f618f19103f7b49dad881ebebd7173b0` |
| | `src/use_after_free.cpp:5:10` | asan | `AddressSanitizer.generic-error` | HIGH | `d36fd9a48ce6be41bd6c24a39cce2188` |
| `-t ubsan` | `src/undefined.cpp:7:17` | ubsan | `UndefinedBehaviorSanitizer.signed-integer-overflow` | HIGH | `57d63250be214c8dbd9df512635daf42` |
| | `src/undefined.cpp:8:17` | ubsan | `UndefinedBehaviorSanitizer.shift` | HIGH | `f05e7a66795951f530cdaa4892631ef0` |
| `-t lsan` | `src/leak.cpp:5:15` | lsan | `LeakSanitizer` | HIGH | `0e0922634385abdb282b0d40245b523d` |
| `-t clang-tidy` | `src/analyzer_input.cpp:7:12` | clang-tidy | `clang-analyzer-core.DivideZero` | UNSPECIFIED | `7504e303eea993b2c28c6da226a94e70` |
| | `src/analyzer_input.cpp:13:3` | clang-tidy | `clang-analyzer-unix.Malloc` | UNSPECIFIED | `4b9e210abce8caf356a4fe15031f19c6` |
| `-t clang-tidy-yaml` | `src/analyzer_input.cpp:7:12` | clang-tidy-yaml | `clang-analyzer-core.DivideZero` | UNSPECIFIED | `7504e303eea993b2c28c6da226a94e70` |
| | `src/analyzer_input.cpp:13:3` | clang-tidy-yaml | `clang-analyzer-unix.Malloc` | UNSPECIFIED | `4b9e210abce8caf356a4fe15031f19c6` |
| `-t gcc` | `src/analyzer_input.cpp:13:3` | gcc | `-Wanalyzer-double-free` | UNSPECIFIED | `cab37a6f7c6a668daa8fef05c634cc0b` |

For example:

```sh
report-converter -t asan -o rc/asan out/heap_overflow.asan.out out/use_after_free.asan.out
CodeChecker parse rc/asan
```

- Every conversion exits 0. All review statuses are `unreviewed`.
- The sanitizer messages contain memory addresses that change from run to
  run, but the report hashes do not: they were the same in a second run, in
  another directory.
- LeakSanitizer works on this machine, although `report-converter.md`
  marks `lsan` as not tested.
- Report locations are relative to the project root. The sanitizer output
  contains absolute paths of wherever the project was built.
