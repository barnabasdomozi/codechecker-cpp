# `analyzer-features`

Bugs found by the different analyzers and checker profiles, bugs with
fix-its, a long bug path and a bug inside a macro.

| File | Contents |
|---|---|
| `memory.cpp` | use after delete, mismatched `new[]`/`delete`, double `free`, leak |
| `paths.cpp` | a null dereference at the end of an 8-step path through a loop, branches and a call |
| `macros.cpp` | a division by zero inside a macro expansion |
| `moves.cpp` | use after `std::move` |
| `style.cpp` | Clang-Tidy findings from the `default`, `sensitive` and `extreme` profiles, several with fix-its, and dead stores |

Used for: [checker-selection.md](../../../docs/usage/checker-selection.md),
[analyzers.md](../../../docs/usage/analyzers.md),
[fixit.md](../../../docs/usage/fixit.md), `parse --print-steps`, macro
expansions in the report formats, and filtering by analyzer, checker and
severity.

## Build

```sh
make
```

## Expected results

### Default checkers

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `macros.cpp:5:36` | clang-tidy | `clang-diagnostic-division-by-zero` | MEDIUM | `bbcdf4a0a721589c8534378cfe34368b` | unreviewed |
| `macros.cpp:5:36` | clangsa | `core.DivideZero` | HIGH | `07807283f6584365e028eb35cac19ac1` | unreviewed |
| `macros.cpp:5:36` | cppcheck | `cppcheck-zerodiv` | HIGH | `3226dadfd58b72f6c4a950ff6d225510` | unreviewed |
| `memory.cpp:11:3` | cppcheck | `cppcheck-deallocret` | HIGH | `cb0532beaf19b480b367a12c24b5af75` | unreviewed |
| `memory.cpp:11:10` | clangsa | `cplusplus.NewDelete` | HIGH | `642600142d719663853164ce08c47008` | unreviewed |
| `memory.cpp:16:3` | clangsa | `unix.MismatchedDeallocator` | MEDIUM | `8ccb39caaf8d0f257f4589df7fce5df3` | unreviewed |
| `memory.cpp:16:10` | cppcheck | `cppcheck-mismatchAllocDealloc` | HIGH | `47801fe8bcd5c413917cc9b0cd69c0ae` | unreviewed |
| `memory.cpp:16:10` | cppcheck | `cppcheck-uninitdata` | HIGH | `e9693e3195fff1189611bc72e9dda5d8` | unreviewed |
| `memory.cpp:22:3` | clangsa | `unix.Malloc` | MEDIUM | `fad81d6cdc809b9207f8dd4de4bd0312` | unreviewed |
| `memory.cpp:22:8` | cppcheck | `cppcheck-doubleFree` | HIGH | `06bc3ad6d7239dba0ceaf73475e51f6f` | unreviewed |
| `memory.cpp:28:5` | cppcheck | `cppcheck-memleak` | HIGH | `d9c541bbd42fc5954622cd6abf6e7310` | unreviewed |
| `memory.cpp:28:12` | clangsa | `cplusplus.NewDeleteLeaks` | HIGH | `48e963206ecb5d0db35f98b4e21691f5` | unreviewed |
| `memory.cpp:29:12` | cppcheck | `cppcheck-uninitdata` | HIGH | `a22065cdab2dae996d42f26338d3b44e` | unreviewed |
| `moves.cpp:11:10` | clang-tidy | `bugprone-use-after-move` | HIGH | `890f36f01563c34b36b8be060c1aa8a0` | unreviewed |
| `moves.cpp:11:10` | clangsa | `cplusplus.Move` | HIGH | `cd452d07fcbca281557b093166e42cd3` | unreviewed |
| `paths.cpp:21:10` | clangsa | `core.NullDereference` | HIGH | `89953e85372f16f4735e60148daad0cc` | unreviewed |
| `style.cpp:7:29` | clang-tidy | `clang-diagnostic-tautological-compare` | MEDIUM | `67e891219d085b3331adb474ef1d2931` | unreviewed |
| `style.cpp:7:29` | clang-tidy | `misc-redundant-expression` | MEDIUM | `131449640afa428eacf74b132858f0d5` | unreviewed |
| `style.cpp:12:22` | clang-tidy | `performance-move-const-arg` | MEDIUM | `d18a4624baa09e9f3079dc4738b1e50b` | unreviewed |
| `style.cpp:21:26` | clang-tidy | `clang-diagnostic-range-loop-construct` | MEDIUM | `0ad3e5ca3afa3748b896a331d843c952` | unreviewed |
| `style.cpp:31:7` | clang-tidy | `clang-diagnostic-unused-but-set-variable` | MEDIUM | `1285003f6090ee00efc50056fee7dff0` | unreviewed |
| `style.cpp:31:7` | clangsa | `deadcode.DeadStores` | LOW | `b01102f2adfd450aa7de85e81799d551` | unreviewed |
| `style.cpp:32:3` | clangsa | `deadcode.DeadStores` | LOW | `4f7787e173ead2abd9960f5f3d6d7ac8` | unreviewed |

- 23 reports (16 from the Clang analyzers, 7 from Cppcheck). `analyze`
  exits 0. `parse` exits 2.
- GCC does not run: the default profile has no GCC checkers
  (`No checkers enabled for gcc`).

`parse --print-steps` for `paths.cpp:21:10`:

```
  Steps:
    1, paths.cpp:16:19: Assuming 'i' is >= 'count'
    2, paths.cpp:16:19: Loop body executed 0 times
    3, paths.cpp:18:3: Calling 'adjust'
    4, paths.cpp:5:1: Entered call from 'long_path'
    5, paths.cpp:6:7: Assuming 'mode' is equal to 1
    6, paths.cpp:7:5: Null pointer value stored to 'target'
    7, paths.cpp:18:3: Returning from 'adjust'
    8, paths.cpp:21:10: Dereference of null pointer (loaded from variable 'target')
```

### `-e profile:sensitive`

27 reports: the 23 above plus:

| Location | Analyzer | Checker | Severity | Report hash |
|---|---|---|---|---|
| `paths.cpp:5:13` | clang-tidy | `misc-use-anonymous-namespace` | LOW | `6caae570a98207ab9dad17ba3a09d63c` |
| `style.cpp:11:21` | clang-tidy | `performance-unnecessary-copy-initialization` | LOW | `e28160385f5b930516a1648c038646f1` |
| `style.cpp:16:47` | clang-tidy | `performance-unnecessary-value-param` | LOW | `0a4672ec836b752b9f1cdb3593cce060` |
| `style.cpp:21:26` | clang-tidy | `performance-for-range-copy` | LOW | `83b81e54156d13a3963468fd6538aa37` |

### `-e profile:extreme`

99 reports, most of them style checks (Cppcheck's 7 are the same as with
the default profile). The profile enables GCC checkers, so
GCC runs and adds 8 reports:

| Location | Checker | Severity | Report hash |
|---|---|---|---|
| `memory.cpp:11:16` | `gcc-analyzer-use-after-free` | HIGH | `98478f9669dcbe3425d55f7a28065d1c` |
| `memory.cpp:16:10` | `gcc-analyzer-mismatching-deallocation` | HIGH | `b567bd0f54c5b9b400148598c541ee23` |
| `memory.cpp:22:3` | `gcc-analyzer-double-free` | HIGH | `7a1d59e41f74391b327db12829fe0b19` |
| `memory.cpp:28:12` | `gcc-analyzer-malloc-leak` | HIGH | `54bc44e57fa19335751299bd4865cb34` |
| `moves.cpp:6:45` | `gcc-analyzer-use-of-uninitialized-value` | HIGH | `64c66cd8b66ae217fccbc18800ee56fb` |
| `moves.cpp:6:45` | `gcc-analyzer-use-of-uninitialized-value` | HIGH | `64c66cd8b66ae217fccbc18800ee56fb` |
| `paths.cpp:21:11` | `gcc-analyzer-null-dereference` | HIGH | `04ebd94ee3c7712ea8126cf22089d10c` |
| `style.cpp:12:38` | `gcc-analyzer-use-of-uninitialized-value` | HIGH | `d8d5f752daf994b61119a85df4bdfae1` |

The `moves.cpp` and `style.cpp` GCC reports are false positives inside
`std::string` code.

### Single checkers

| Option | Result |
|---|---|
| `-e modernize-use-nullptr` | 24 reports: the default 23 plus `style.cpp:27:30` `modernize-use-nullptr` (LOW, `6c402b0d0f171d950520975c981b4c50`) |
| `-e gcc-analyzer-double-free` | 24 reports: the default 23 plus the GCC `memory.cpp:22:3` report above; GCC runs because one of its checkers is enabled |

### Fix-its

`CodeChecker fixit` needs `clang-apply-replacements` on `PATH`. On Debian
it is in `/usr/lib/llvm-19/bin`. Without it, `fixit` prints
`clang-apply-replacements tool is not found` and exits 0.

| Analysis | Fix-its listed by `CodeChecker fixit reports` |
|---|---|
| default | `performance-move-const-arg` |
| `-e profile:sensitive` | `performance-for-range-copy`, `performance-move-const-arg`, `performance-unnecessary-copy-initialization`, `performance-unnecessary-value-param` |
| `-e modernize-use-nullptr` | `performance-move-const-arg`, `modernize-use-nullptr` |
