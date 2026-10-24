# `build-failures`

A build that fails, and sources that build but cannot be analyzed.

Used for: the exit code of `log` and `check -b` when the build fails,
`analyze` exit code 3, the `reports/failed/` folder, and the GCC options
CodeChecker removes before running Clang
([analyze.md](../../../docs/usage/analyze.md),
[exit-codes.md](../../../docs/usage/exit-codes.md),
[compiler-info.md](../../../docs/reference/compiler-info.md) §2.1).

| File | Builds | Analysis |
|---|---|---|
| `good.cpp` | yes | succeeds |
| `gcc_only_flag.cpp` | yes, with `-fconcepts-diagnostics-depth=2` | fails: Clang rejects the option and CodeChecker does not remove it |
| `ignored_flag.cpp` | yes, with `-fsched-pressure` | succeeds: Clang would reject the option, but it is on CodeChecker's GCC ignore list |
| `broken_syntax.cpp` | no: missing `;` | fails |
| `missing_header.cpp` | no: `#include "does_not_exist.h"` | fails |

## Build

```sh
make      # stops at broken_syntax.cpp
make -k   # also tries missing_header.cpp
```

Both exit 2. `CodeChecker log` exits 2 too (the build's exit code), but it
still writes the compilation database. Failing compiler calls are logged
as well:

- `make`: 4 entries (all files except `missing_header.cpp`);
- `make -k`: 5 entries.

## Expected results

### `make -k`

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `broken_syntax.cpp:2:16` | clang-tidy | `clang-diagnostic-unused-parameter` | MEDIUM | `99dd5f454683f1ab35a1c06ecc3355c3` | unreviewed |
| `broken_syntax.cpp:3:12` | clang-tidy | `clang-diagnostic-error` | CRITICAL | `b6fd4eadba03ca0e7f33c5545f59d269` | unreviewed |
| `good.cpp:5:12` | clangsa | `core.DivideZero` | HIGH | `22dbad23af0073e135c0d28d3a0599ec` | unreviewed |
| `ignored_flag.cpp:6:12` | clangsa | `core.NullDereference` | HIGH | `5abd775762e6087041c084d98f60bc90` | unreviewed |
| `missing_header.cpp:2:10` | clang-tidy | `clang-diagnostic-error` | CRITICAL | `2f16c4902139fbaee7b9a8307ae56a37` | unreviewed |

- `analyze` exits 3 (some analysis failed). `parse` exits 2.
- Summary: successfully analyzed clangsa 2, clang-tidy 2; failed clangsa 3,
  clang-tidy 3.
- `reports/failed/` has one `<file>_<analyzer>_<hash>.plist_compile_error.zip`
  per failed analysis: `broken_syntax.cpp`, `gcc_only_flag.cpp` and
  `missing_header.cpp`, each for clangsa and clang-tidy (6 files).
- Clang-Tidy still writes reports for files that do not compile
  (`clang-diagnostic-error`). `gcc_only_flag.cpp` gives no report at all.

### `make`

The same, without the `missing_header.cpp` report: 4 reports, and failed
clangsa 2, clang-tidy 2 (4 files in `reports/failed/`). `analyze` exits 3.

### `CodeChecker check -b make`

The build fails, so `check` logs `Build failed.`, does **not** analyze, and
exits with the build's exit code, 2. That is the same value as "reports
found", so a test has to check the output too.
