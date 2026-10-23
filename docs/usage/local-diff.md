# Compare results without a server (`cmd diff` on local folders)

`CodeChecker cmd diff` also works without a server. Both sides can be local
report folders or baseline files. This is useful in CI to fail a build only on
**new** reports.

```
CodeChecker cmd diff -b BASE [...] -n NEW [...] (--new | --resolved | --unresolved)
                     [filters] [-o FORMAT ...] [-e EXPORT_DIR] [-c] [--print-steps]
```

Server runs work too; see [cmd-diff.md](cmd-diff.md). Reports are matched by
report hash (see `--report-hash` in [analyze.md](analyze.md)). False-positive
and intentional reports count as resolved.

Examples use `reports` (from `~/demo`) and `reports2` (from `~/demo2`, see
[demo-project.md](demo-project.md)).

## New reports ✅

```sh
CodeChecker cmd diff -b reports -n reports2 --new
```

```
[INFO] - Matching local report directories (--baseline): ~/reports
[INFO] - Matching local report directories (--newname): ~/reports2
[HIGH] ~/demo2/main.c:21:18: Dereference of null pointer (loaded from variable 'q') [core.NullDereference]
  int *q = 0; *q = 1;
                 ^

[LOW] ~/demo2/style.cpp:4:12: use nullptr [modernize-use-nullptr]
...
[HIGH] ~/demo2/style.cpp:4:35: Division by zero [core.DivideZero]
...
----======== Summary ========----
┌───────────────────────────────────────────┬───┐
│ Number of processed analyzer result files │ 0 │
├───────────────────────────────────────────┼───┤
│ Number of analyzer reports                │ 3 │
└───────────────────────────────────────────┴───┘
----=================----
[INFO] - Compared the following local files / directories: ~/reports and ~/reports2
```

Exit code `2` (differences found).

(`reports2` was analyzed with `-e modernize-use-nullptr`, so that report is
also new.)

## Resolved and unresolved reports ✅

```sh
CodeChecker cmd diff -b reports -n reports2 --resolved          # 1 report: the unused variable
CodeChecker cmd diff -b reports -n reports2 --unresolved -o table
```

```
┌──────┬──────────────────────┬──────────────────┬─────────────────┬─────────────────┐
│ File │ Checker              │ Severity         │ Msg             │ Source          │
├──────┼──────────────────────┼──────────────────┼─────────────────┼─────────────────┤
...
│ HIGH │ ~/demo2/main.c:11:12 │ Division by zero │ core.DivideZero │   return a / z; │
└──────┴──────────────────────┴──────────────────┴─────────────────┴─────────────────┘
```

⚠️ The column headers do not match the data: the "File" column holds the
severity, "Checker" the file, "Severity" the message and "Msg" the checker.
See [known-issues.md](known-issues.md).

## Compare against a baseline file ✅

A baseline file holds only report hashes, so it can be committed to the
repository (see [parse-export.md](parse-export.md)).

```sh
CodeChecker parse reports -e baseline -o reports.baseline
CodeChecker cmd diff -b reports.baseline -n reports2 --new
```

```
[INFO] - Matching local baseline files (--baseline): ~/reports.baseline
[INFO] - Matching local report directories (--newname): ~/reports2
[HIGH] ~/demo2/main.c:21:18: Dereference of null pointer (loaded from variable 'q') [core.NullDereference]
[LOW] ~/demo2/style.cpp:4:12: use nullptr [modernize-use-nullptr]
[HIGH] ~/demo2/style.cpp:4:35: Division by zero [core.DivideZero]
```

A baseline has only hashes, so reports can only be printed from the folder
side:
- baseline as `-b`, folder as `-n`: `--new` prints reports in the folder that
  are not in the baseline.
- folder as `-b`, baseline as `-n`: `--resolved` prints reports in the folder
  that are not in the baseline ✅. ⚠️ `--new` then finds 0 reports but still
  exits with code `2`.

## No differences ✅

```sh
CodeChecker cmd diff -b reports -n reports --new      # exit code 0
```

## Filters ✅

All result filters of `cmd results` work (see
[cmd-results.md](cmd-results.md)), e.g.:

```sh
CodeChecker cmd diff -b reports -n reports2 --new --severity high --checker-name "core.*" --print-steps
```

```
[HIGH] ~/demo2/main.c:21:18: Dereference of null pointer (loaded from variable 'q') [core.NullDereference]
  int *q = 0; *q = 1;
                 ^
  Report hash: 19a15bf46b7d3af4b189b3b4f2c28a5b
  Steps: ...
```

`--report-status`, `--detection-status`, `--tag` and date filters only make
sense for server runs. For local folders they are accepted without error.

## Output formats ✅

`-o` takes one or more of: `plaintext` (default), `rows`, `table`, `csv`,
`json`, `html`, `gerrit`, `codeclimate`.

```sh
CodeChecker cmd diff -b reports -n reports2 --new -o html -e ./diff_html
CodeChecker cmd diff -b reports -n reports2 --new -o json -e ./diff_json        # -> reports.json
CodeChecker cmd diff -b reports -n reports2 --new -o json                       # JSON to stdout
CodeChecker cmd diff -b reports -n reports2 --new -o codeclimate -e ./diff_cc   # -> codeclimate_issues.json
CC_CHANGED_FILES=changed.json CC_REPO_DIR=~/demo2 \
  CodeChecker cmd diff -b reports -n reports2 --new -o gerrit -e ./diff_gerrit  # -> gerrit_review.json
CodeChecker cmd diff -b reports -n reports2 --new -o plaintext html -e ./out -c # several formats; -c cleans ./out first
```

```
To view the results in a browser run:
> firefox ./diff_html/index.html
```

```
[INFO] - JSON report file was created: ./diff_json/reports.json
```

`html` needs `-e`. Without it, the error message names `parse`'s options:

```
[ERROR] - Argument --export not allowed without argument --output when exporting to HTML.
```

For `json`, `gerrit` and `codeclimate`, the output goes to a file if `-e` is
given, otherwise to standard output.

## Exit codes ✅

| Code | Meaning |
|---|---|
| 0 | No difference |
| 1 | CodeChecker error |
| 2 | At least one report in the difference |
