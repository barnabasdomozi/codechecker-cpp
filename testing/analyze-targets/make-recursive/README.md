# `make-recursive`

A recursive Make build: the top-level `Makefile` runs `make -C core` and
`make -C tools`. Most bugs only exist if a compiler option reaches the
analyzer, so this project checks that build commands are logged and parsed
correctly.

Used for:

- `CodeChecker log` with recursive Make ([log.md](../../../docs/usage/log.md));
- macros (`-D`), a force-included header (`-include ../config.h`) and a
  response file (`@flags.rsp`)
  ([compiler-info.md](../../../docs/reference/compiler-info.md));
- one source file (`core/codec.cpp`) compiled twice with different options:
  `--compile-uniqueing` ([analyze.md](../../../docs/usage/analyze.md));
- `analyze --makefile` ([makefile-mode.md](../../../docs/usage/makefile-mode.md)).

## Build

```sh
make
```

The compilation database has 4 entries: `core/buffer.cpp`, `core/codec.cpp`
twice (`-o codec_fast.o` with `-DCODEC_FAST`, `-o codec_safe.o` without),
and `tools/cli.cpp`. The `tools/cli.cpp` command still contains
`@flags.rsp`; `analyze` expands it.

## Expected results

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `core/buffer.cpp:11:29` | clang-tidy | `clang-diagnostic-division-by-zero` | MEDIUM | `9886ed0410290a9839cece481887aae3` | unreviewed |
| `core/buffer.cpp:11:29` | clangsa | `core.DivideZero` | HIGH | `4ceebfd2af13d60f352c65c4ce6851b0` | unreviewed |
| `core/buffer.cpp:20:10` | clangsa | `core.NullDereference` | HIGH | `76079336dd9f27777c81917b2503b383` | unreviewed |
| `core/codec.cpp:10:11` | clangsa | `core.NullDereference` | HIGH | `fe0afaf52ab95f45da71275d987d4a55` | unreviewed |
| `core/codec.cpp:14:27` | clangsa | `core.DivideZero` | HIGH | `fe56b9074153abc232cfdc4e65c4d8ac` | unreviewed |
| `tools/cli.cpp:10:27` | clangsa | `core.DivideZero` | HIGH | `ef781612330e0fbe66cf77b875db2564` | unreviewed |

`analyze` exits 0. `parse` exits 2.

What each report depends on:

| Report | Needs |
|---|---|
| `core/buffer.cpp:11` | `config.h` force-included (`-include`); without it the file does not compile |
| `core/buffer.cpp:20` | `-DCHECKED_BUFFER` |
| `core/codec.cpp:10` | `-DCODEC_FAST`, so only the `codec_fast.o` entry |
| `core/codec.cpp:14` | only the `codec_safe.o` entry: in the fast variant, the null dereference on line 10 ends the path first |
| `tools/cli.cpp:10` | `-I../core` and `-DTOOL_LEVEL=2` from the response file |

### Compile uniqueing

| `--compile-uniqueing` | Exit | `codec.cpp` reports |
|---|---|---|
| `none` (default) | 0 | both lines 10 and 14 |
| `strict` | 1 | none; `Build Action uniqueing failed as both '/usr/bin/g++ ... -DCODEC_FAST -c codec.cpp -o codec_fast.o' and ...` |
| `alpha` | 0 | line 10 only (keeps `codec_fast.o`, the alphabetically first output) |
| `.*-DCODEC_FAST.*` | 0 | line 10 only |
| `(?!.*CODEC_FAST)` | 0 | line 14 only |

The regex is matched from the start of the command (`re.match`), so a
pattern like `-DCODEC_FAST` alone matches nothing.
