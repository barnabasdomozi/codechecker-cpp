# Collect translation unit sources (`tu_collector`)

`tu_collector` has two jobs:
1. Pack every source and header file needed to compile some translation units
   into a zip file, keeping the folder structure. Useful for bug reports and
   for moving an analysis to another machine.
2. List the source files that include a given header.

It works from a compilation database (`-l`) or a single compiler command
(`-b`).

```
tu_collector (-l COMPILATION_DATABASE | -b COMPILER_COMMAND)
             (-z ZIP_FILE | -d) [-f FILTER] [--ctu-deps-dir DIR] [-v]
```

## Zip all translation units ✅

```sh
tu_collector -l compile_commands.json -z tu.zip
```

```
[INFO] - Done.
```

```
tu.zip
├── compilation_database.json
└── sources-root/
    ├── home/user/demo/main.c
    ├── home/user/demo/style.cpp
    ├── home/user/demo/lib/util.c
    ├── home/user/demo/lib/util.h
    ├── usr/include/stdio.h
    ├── usr/include/c++/14/cstddef
    └── ...                                  # every system header used
```

## Zip only some translation units (`-f`) ⚠️

```sh
tu_collector -l compile_commands.json -z tu_main.zip -f main.c
tu_collector -l compile_commands.json -z tu_main.zip -f "*main.c"
```

The zip has `main.c`, `lib/util.h`, the system headers and
`compilation_database.json`.

⚠️ With `-z`, the filter is a glob matched against the **`file` field of the
compilation database as written**. That field is often relative (`main.c`),
so absolute patterns like `/home/user/demo/main.c` or `*/demo/main.c` match
nothing. The zip then has only `compilation_database.json`, with no error. The
help text suggests absolute patterns (`/path/to/*/files`).

## Include CTU dependencies ✅

```sh
CodeChecker analyze compile_commands.json -o reports --ctu-collect --analyzers clangsa
CodeChecker analyze compile_commands.json -o reports --ctu-analyze --analyzers clangsa
tu_collector -l compile_commands.json -z tu_ctu.zip -f main.c --ctu-deps-dir reports/ctu_connections
```

The zip now also has `lib/util.c`, because the CTU analysis of `main.c` used
it.

## Zip one compiler command (`-b`) ✅

```sh
tu_collector -b "gcc -c main.c -o /dev/null" -z t2.zip
```

The zip has `main.c` and `lib/util.h`. `-b` takes **one compiler call**, not a
build system command. With `-b "make"`, it prints
`[ERROR] - Couldn't create dependencies:`, writes a zip with only
`compilation_database.json`, and still exits `0`. `-f` and `--ctu-deps-dir`
are ignored with `-b` (with a warning).

## List the sources that include a header (`-d`) ✅

```sh
tu_collector -l compile_commands.json -d -f "*/lib/util.h"
```

```
/home/user/demo/lib/util.c
/home/user/demo/main.c
```

Here `-f` is a header path pattern matched against absolute paths. Headers are
not listed, even when they include the header.

## Exit code

`0`, even when nothing was collected. `1` if the compilation database file
does not exist.
