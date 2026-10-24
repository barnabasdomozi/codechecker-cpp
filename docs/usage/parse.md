# Print results in the terminal (`CodeChecker parse`)

`CodeChecker parse` reads one or more report folders or result files and
prints the reports with summary tables. It can also export them to other
formats (see [parse-export.md](parse-export.md)).

```
CodeChecker parse FILE_OR_FOLDER [...] [--print-steps] [--status [--detailed]]
                  [--trim-path-prefix PREFIX ...] [--review-status STATUS ...]
                  [--file PATTERN ...] [-i SKIPFILE]
                  [--suppress FILE [--export-source-suppress]]
                  [-e FORMAT [-o OUTPUT]] [--config FILE] [-t plist]
```

Reports hidden by `false_positive`, `intentional` or `suppress` source-code
comments are not printed by default (see
[review-status-and-suppression.md](review-status-and-suppression.md)).

## Print a report folder ✅

```sh
CodeChecker parse ./reports
```

```
Found no defects in style.cpp
Found no defects in util.c
[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]
  int unused = 5;
      ^

[HIGH] ~/demo/main.c:25:10: variable 'x' is uninitialized when used here [clang-diagnostic-uninitialized]
  return x;
         ^

Found 2 defect(s) in main.c

[HIGH] ~/demo/style.cpp:11:12: Division by zero. [cppcheck-zerodiv]
  return a / z;
           ^

Found 1 defect(s) in style.cpp

[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]
  return a / z;
           ^

Found 1 defect(s) in main.c

[HIGH] ~/demo/main.c:11:12: Division by zero. [cppcheck-zerodiv]
  return a / z;
           ^

[HIGH] ~/demo/main.c:25:10: Uninitialized variable: x [cppcheck-uninitvar]
  return x;
         ^

Found 2 defect(s) in main.c


----==== Severity Statistics ====----
┌──────────┬───────────────────┐
│ Severity │ Number of reports │
├──────────┼───────────────────┤
│ MEDIUM   │ 1                 │
│ HIGH     │ 5                 │
└──────────┴───────────────────┘
----=================----

----==== Checker Statistics ====----
┌──────────────────────────────────┬──────────┬───────────────────┐
│ Checker name                     │ Severity │ Number of reports │
├──────────────────────────────────┼──────────┼───────────────────┤
│ clang-diagnostic-unused-variable │ MEDIUM   │ 1                 │
│ clang-diagnostic-uninitialized   │ HIGH     │ 1                 │
│ cppcheck-zerodiv                 │ HIGH     │ 2                 │
│ core.DivideZero                  │ HIGH     │ 1                 │
│ cppcheck-uninitvar               │ HIGH     │ 1                 │
└──────────────────────────────────┴──────────┴───────────────────┘
----=================----

----==== File Statistics ====----
┌───────────┬───────────────────┐
│ File name │ Number of reports │
├───────────┼───────────────────┤
│ main.c    │ 5                 │
│ style.cpp │ 1                 │
└───────────┴───────────────────┘
----=================----

----======== Summary ========----
┌───────────────────────────────────────────┬───┐
│ Number of processed analyzer result files │ 9 │
├───────────────────────────────────────────┼───┤
│ Number of analyzer reports                │ 6 │
└───────────────────────────────────────────┴───┘
----=================----
[INFO] - ----==== Summary ====----
[INFO] - Up-to-date analysis results
[INFO] -   clangsa: 3
[INFO] -   clang-tidy: 3
[INFO] -   cppcheck: 3
[INFO] - Outdated analysis results
[INFO] - Failed to analyze
[INFO] - Missing analysis results
[INFO] -   gcc: 3
[INFO] -   infer: 3
[INFO] - Total analyzed compilation commands: 3
[INFO] - Total available compilation commands: 3
[INFO] - ----=================----
```

The report blocks are grouped by result file, so reports from Clang SA,
Clang-Tidy and Cppcheck for the same source file appear in separate groups.
The order of the groups is not fixed. The Cppcheck report on `style.cpp:11`
is shown because its comment names `core.DivideZero` only (see
[demo-project.md](demo-project.md)). The final
`[INFO]` block is the analysis status summary (see `--status` below).

## Parse one result file or several folders ✅

```sh
CodeChecker parse reports/main.c_clangsa_681db965dea6200719ba62bde2ba8cb4.plist
CodeChecker parse ./reports_a ./reports_b
```

## Print the bug path ✅

```sh
CodeChecker parse ./reports --print-steps
```

```
[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]
  return a / z;
           ^
  Report hash: 09296549497056a329a9ce22dda7d710
  Steps:
    1, main.c:22:18: Calling 'local_div'
    2, main.c:9:1: Entered call from 'main'
    3, main.c:10:3: 'z' initialized to 0
    4, main.c:11:12: Division by zero
```

## Analysis status ✅

```sh
CodeChecker parse ./reports --status
CodeChecker parse ./reports --status --detailed
```

Prints only the `[INFO] Summary` block. It counts per analyzer the files that
are up to date, outdated (source changed after analysis), failed, and missing
(never analyzed). `--detailed` lists the file names. See
[analysis-debugging.md](analysis-debugging.md) for an example with a failure.
With `--status`, the exit code is `0`.

The status can also be exported as JSON ✅ (`-o FILE` writes it to a file):

```sh
CodeChecker parse ./reports --status -e json
CodeChecker parse ./reports --status --detailed -e json -o status.json
```

```json
{
  "analyzers": {
    "clangsa": {
      "summary": {
        "up-to-date": 3,
        "outdated": 0,
        "missing": 0,
        "failed": 0
      }
    },
    ...
  },
  "total_analyzed_compilation_commands": 3,
  "total_available_compilation_commands": 3
}
```

With `--detailed`, each analyzer also lists the file names under
`up-to-date`, `outdated`, `missing` and `failed`. Other export formats are
rejected: `Only JSON export format is supported.` (exit 1). `--status --file F`
limits the status to source files `F` (they must exist).

## Shorten file paths ✅

```sh
CodeChecker parse ./reports --trim-path-prefix "$HOME/"
```

```
[MEDIUM] demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]
```

Several prefixes can be given; the longest match is removed. Shell-style
wildcards are allowed, e.g. `'/*/user/'`.

## Show only some files ✅

```sh
CodeChecker parse ./reports --file "*/main.c"
CodeChecker parse ./reports -i skipfile.txt
```

`--file` takes patterns with `*`. `-i/--ignore/--skip` takes a skipfile (see
[skip-and-file-filters.md](skip-and-file-filters.md)).

## Filter by review status ✅

```sh
CodeChecker parse ./reports --review-status intentional
CodeChecker parse ./reports --review-status unreviewed confirmed false_positive intentional suppress
```

```
[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable] [Intentional]
```

The default is `confirmed unreviewed`. For SARIF export the default is all
statuses. Reports with a review status other than unreviewed show it at the
end of the line, e.g. `[False positive]`.

## Suppress files ✅

```sh
CodeChecker parse ./reports --suppress suppress.txt
CodeChecker parse ./reports --export-source-suppress --suppress suppress.txt
```

See [review-status-and-suppression.md](review-status-and-suppression.md).

## Exit codes ✅

| Code | Meaning |
|---|---|
| 0 | No reports printed (or `--status`) |
| 1 | CodeChecker error |
| 2 | At least one report printed or exported |
