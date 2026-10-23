# Run the analyzers (`CodeChecker analyze`)

`CodeChecker analyze` reads a compilation database, runs every enabled
analyzer on every compilation command, and writes the results into a report
folder. It prints only progress; use [`parse`](parse.md) or [`store`](store.md)
to look at the results.

```
CodeChecker analyze INPUT -o OUTPUT_DIR [options]
```

The options are described on these pages:

| Topic | Page |
|---|---|
| Which checkers run | [checker-selection.md](checker-selection.md) |
| Analyzer and checker options | [analyzer-configuration.md](analyzer-configuration.md) |
| Compiler include paths, target, standard | [compiler-info.md](compiler-info.md) |
| Options from a file | [config-file.md](config-file.md) |
| Skip files / analyze some files only | [skip-and-file-filters.md](skip-and-file-filters.md) |
| Review status rules | [review-status-and-suppression.md](review-status-and-suppression.md) |
| Cross translation unit analysis | [ctu-analysis.md](ctu-analysis.md) |
| Statistical analysis | [statistical-analysis.md](statistical-analysis.md) |
| Generate a Makefile | [makefile-mode.md](makefile-mode.md) |
| Captured output, reproducers, failures, timeout | [analysis-debugging.md](analysis-debugging.md) |

This page covers inputs, outputs and general options.

## Analyze a compilation database ✅

```sh
cd ~/demo
CodeChecker analyze compile_commands.json -o ./reports
```

```
[INFO] - CodeChecker analyzer version: ...
[WARNING] - Can't analyze with 'infer': Failed to detect analyzer binary!
[WARNING] - Can't analyze with 'cppcheck': Failed to detect analyzer binary!
[INFO] - No checkers enabled for gcc
[INFO] - Enabled checker list can be found in ./reports/metadata.json
[INFO] - Starting static analysis ...
[INFO] - [1/6] clangsa analyzed util.c successfully.
[INFO] - [2/6] clangsa analyzed main.c successfully.
[INFO] - [3/6] clangsa analyzed style.cpp successfully.
[INFO] - [4/6] clang-tidy analyzed main.c successfully.
[INFO] - [5/6] clang-tidy analyzed util.c successfully.
[INFO] - [6/6] clang-tidy analyzed style.cpp successfully.
[INFO] - ----==== Summary ====----
[INFO] - Successfully analyzed
[INFO] -   clangsa: 3
[INFO] -   clang-tidy: 3
[INFO] - Total analyzed compilation commands: 3
[INFO] - ----=================----
[INFO] - Analysis finished.
[INFO] - To view results in the terminal use the "CodeChecker parse" command.
[INFO] - To store results use the "CodeChecker store" command.
...
[INFO] - Analysis length: 0.18 sec.
```

By default, every analyzer that is found and has enabled checkers runs. GCC
has no checkers on by default, so it is skipped.

### What the report folder contains

```
reports/
├── metadata.json                         # command, versions, enabled checkers, run name, timestamps
├── compile_cmd.json                      # the compilation database that was used
├── unique_compile_commands.json          # after compile uniqueing (see below)
├── compiler_info.json                    # detected implicit includes / target / standard
├── main.c_clangsa_<hash>.plist           # one result file per source file and analyzer
├── main.c_clang-tidy_<hash>.plist
├── ...
├── conf/                                 # analyzer config files used (e.g. clang-tidy config)
├── fixit/                                # clang-tidy fix-it YAML files (used by `fixit`)
├── ctu_connections/                      # which TUs each analysis used (CTU)
├── cppcheck/                             # cppcheck working directory
├── success/                              # only with --capture-analysis-output
├── failed/                               # zip files for failed analyses
└── reproducer/                           # only with --generate-reproducer
```

The `<hash>` is computed from the compilation command. The same command
always produces the same file name, so a later analysis overwrites old
results.

The report format is plist (`-t/--type/--output-format plist`, the only
choice).

## Analyze one source file ✅

```sh
CodeChecker analyze main.c -o ./reports
```

`INPUT` can be a C/C++/Objective-C source file. CodeChecker looks for the
nearest `compile_commands.json` in the file's parent folders and uses that
file's entries for this source file.

⚠️ If no `compile_commands.json` is found in a parent folder, or it has no
entry for the file, nothing is analyzed:

```
[WARNING] - No analysis is required.
[WARNING] - There were no compilation commands in the provided compilation database or all of them were skipped.
```

The exit code is still `0`.

## Analyze a folder ✅

```sh
CodeChecker analyze lib -o ./reports
```

`INPUT` can be a folder. CodeChecker collects every `compile_commands.json`
inside the folder and in its parent folders, and analyzes the entries whose
source file is inside the folder. When several databases describe the same
file, the one in the closest parent folder is used. In the demo, only
`lib/util.c` is analyzed.

## Run only some analyzers ✅

```sh
CodeChecker analyze compile_commands.json -o ./reports --analyzers clangsa
CodeChecker analyze compile_commands.json -o ./reports --analyzers clang-tidy gcc
```

Valid names: `clangsa`, `clang-tidy`, `cppcheck`, `gcc`, `infer`.
❌ `cppcheck` and `infer` could not be run on the test machine.

## Number of parallel jobs ✅

```sh
CodeChecker analyze compile_commands.json -o ./reports -j 1
```

The default is the number of CPU cores.

## Hide analyzer output ✅

```sh
CodeChecker analyze compile_commands.json -o ./reports -q
```

The analyzers' standard output and error are not printed.

## Clean the output folder first ✅

```sh
CodeChecker analyze compile_commands.json -o ./reports -c
```

Without `-c`, old result files are kept and only files for the analyzed
commands are overwritten. This allows incremental analysis: analyze changed
files only, and the folder still holds results for the whole project.

## Name the run ✅

```sh
CodeChecker analyze compile_commands.json -o ./reports -n my_run
```

Saves `"run_name": "my_run"` in `metadata.json`. `CodeChecker store` uses this
name when `store -n` is not given.

## Compile uniqueing ✅

A compilation database can list the same source file more than once, for
example when it is built for two configurations. `--compile-uniqueing`
decides what happens:

| Value | Behavior |
|---|---|
| `none` (default without CTU) | Analyze every entry. |
| `strict` | Error out if a file has more than one entry. |
| `symlink` | Treat paths that point to the same file through symlinks as duplicates and keep one. |
| `alpha` (default with CTU) | Keep the entry whose output target is alphabetically first. |
| any other text | A Python regex. Keep the one entry whose command matches; error if more than one matches. |

```sh
CodeChecker analyze dup.json -o ./reports --compile-uniqueing strict
```

```
[ERROR] - Build Action uniqueing failed as both '/usr/bin/gcc -c main.c -o main.o' and '/usr/bin/gcc -DVARIANT -c main.c -o main.o'
```
(exit code 1)

```sh
CodeChecker analyze dup.json -o ./reports --compile-uniqueing ".*VARIANT.*"
```

Keeps only the `-DVARIANT` command. The chosen commands are written to
`reports/unique_compile_commands.json`.

## Report hash method ✅

```sh
CodeChecker analyze compile_commands.json -o ./reports --report-hash context-free-v2
```

Each report gets a hash that identifies it across analyses. The hash decides
whether a report is "new" or "the same as before" in `diff` and on the
server. Values:

| Value | Hash is based on |
|---|---|
| (default) | Clang SA: enclosing function signature, line content, checker name, column. Clang-Tidy: file name, checker name, message, line content, column numbers, control/event columns. |
| `context-free` | Kept for backward compatibility. |
| `context-free-v2` | File name, message, line content with whitespace removed, column. For Clang SA and Clang-Tidy. **Recommended.** |
| `diagnostic-message` | Like `context-free-v2` plus the messages of all bug path steps. |

## Exit codes ✅

| Code | Meaning |
|---|---|
| 0 | Analysis finished (findings do not change the exit code) |
| 1 | CodeChecker error (bad arguments, unknown checker, uniqueing error, ...) |
| 3 | At least one translation unit failed to analyze |
| 128+N | Stopped by signal N |
