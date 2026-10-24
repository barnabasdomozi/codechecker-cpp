# `odd-paths`

Directory and file names with spaces, non-ASCII characters and an
apostrophe, and a macro whose value is a string with a space. Built by a
shell script instead of Make.

Used for: path handling everywhere paths are written or read: the
compilation database, plist and JSON output, HTML export, skipfiles,
`store` and the server's file paths.

```
odd-paths/
├── build.sh
├── include dir/shared header.h
└── src dir/
    ├── main file.cpp
    ├── quote's.cpp
    └── ünïcødé/naïve.cpp
```

## Build

```sh
CodeChecker log -b ./build.sh -o compile_commands.json
```

The logged commands escape spaces with backslashes, for example
`/usr/bin/g++ -std=c++17 -Iinclude\ dir -c src\ dir/main\ file.cpp -o out\ dir/main\ file.o`
and `-DGREETING=\"hello\ world\"`. The apostrophe in `quote's.cpp` is not
escaped, and the analysis still works.

## Expected results

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `src dir/main file.cpp:8:10` | clangsa | `core.NullDereference` | HIGH | `e441301cf2d65140845ed81aaefc4bc1` | unreviewed |
| `src dir/quote's.cpp:6:7` | clang-tidy | `clang-diagnostic-unused-variable` | MEDIUM | `2ce3aa0f147913184b5d0ee939b069ae` | unreviewed |
| `src dir/ünïcødé/naïve.cpp:7:14` | clangsa | `core.DivideZero` | HIGH | `8e07f4fac9e570c44ed0a98b9613cc5d` | unreviewed |

`analyze` exits 0. `parse` exits 2.

`naïve.cpp` also has a comment with German and Japanese text, so source
lines with non-ASCII bytes are read too.
