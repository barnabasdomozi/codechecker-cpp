# Skip files and analyze only some files

## Skipfile ✅

A skipfile lists path patterns, one per line. A line starting with `-`
excludes matching files and `+` includes them. The **first matching line
wins**. `*` matches any characters, including `/`. Files that match no line
are included.

`skip.txt`:

```
-*/lib/*
```

```sh
CodeChecker analyze compile_commands.json -o reports -i skip.txt
```

Only `main.c` and `style.cpp` are analyzed; `lib/util.c` is not. `-i` has
two other names: `--ignore` and `--skip`.

Lines starting with `#` are comments ✅. Example:

```
# analyze this one file in lib/
+*/lib/important.c
# skip the rest of lib/
-*/lib/*
# absolute path
-/usr/*
# any path part starting with "test"
-*/test*
```

A skipfile that includes only some files and skips everything else ✅:

```
+*/main.c
-*
```

The analysis log reports how many commands were skipped:
`[INFO] - Skipped compilation commands: 1`.

### Drop reports from skipped files ✅

```sh
CodeChecker analyze compile_commands.json -o reports -i skip.txt --drop-reports-from-skipped-files
```

Removes reports located in skipped files, even if they come from analyzing a
file that was not skipped.

### Skipfile with `parse` and `check` ✅

`parse` also takes `-i/--ignore/--skip SKIPFILE`, to hide results from skipped
files:

```sh
printf -- '-*/main.c\n' > skip_main.txt
CodeChecker parse reports -i skip_main.txt     # no reports printed, exit code 0
```

`check` passes `-i` to its analyze step.

## Analyze only some files (`--file`) ✅

```sh
CodeChecker analyze compile_commands.json -o reports --file "*/style.cpp"
CodeChecker analyze compile_commands.json -o reports --file /home/user/demo/main.c "*/lib/*.c"
```

Only entries whose source file matches are analyzed. Absolute patterns start
with `/`; relative patterns must start with `*`. Several patterns can be
given.

⚠️ `--file` only matches the `file` entries of the compilation database
(source files). A header pattern such as `--file "*/lib/util.h"` matches
nothing in 6.30.0:

```
[WARNING] - There were no compilation commands in the provided compilation database or all of them were skipped.
```

To find the sources that include a header, use
`tu_collector -d -f <header>` (see [tu-collector.md](tu-collector.md)).

## Filter printed results by file (`parse --file`) ✅

```sh
CodeChecker parse reports --file "*/main.c"
```

Shows only reports in matching files. The analysis is not changed. See
[parse.md](parse.md).

## Skipfile from Gerrit changed files ✅

`gerrit_changed_files_to_skipfile.py` turns a Gerrit changed-files list into
a skipfile, so only changed files are analyzed. See
[gerrit-integration.md](gerrit-integration.md).
