# Checker statistics (`cmd sum`)

`cmd sum` counts reports per checker and per severity for one or more runs.

```
CodeChecker cmd sum (-n RUN_NAME [...] | -a) [filters] [--filter-preset NAME]
                    [-o {plaintext,rows,table,csv,json}] [--url PRODUCT_URL]
```

All filters of [`cmd results`](cmd-results.md) are accepted. The difference:
`--uniqueing` defaults to `on` here.

## Statistics for a run ✅

```sh
CodeChecker cmd sum -n demo --url localhost:8555/Default
```

The output has three tables. Per checker:

```
┌────────────────────────────────┬──────────┬────────────┬───────────┬────────────────┬─────────────┬─────────────┐
│ Checker                        │ Severity │ Unreviewed │ Confirmed │ False positive │ Intentional │ All reports │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ clang-diagnostic-uninitialized │ HIGH     │ 1          │ 0         │ 0              │ 0           │ 1           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ core.DivideZero                │ HIGH     │ 2          │ 0         │ 2              │ 0           │ 2           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ core.NullDereference           │ HIGH     │ 1          │ 0         │ 0              │ 0           │ 1           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ cppcheck-nullPointer           │ HIGH     │ 1          │ 0         │ 0              │ 0           │ 1           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ cppcheck-uninitvar             │ HIGH     │ 1          │ 0         │ 0              │ 0           │ 1           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ cppcheck-zerodiv               │ HIGH     │ 3          │ 0         │ 1              │ 0           │ 3           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ modernize-use-nullptr          │ LOW      │ 1          │ 0         │ 0              │ 0           │ 1           │
├────────────────────────────────┼──────────┼────────────┼───────────┼────────────────┼─────────────┼─────────────┤
│ Total                          │ -        │ 10         │ 0         │ 3              │ 0           │ 10          │
└────────────────────────────────┴──────────┴────────────┴───────────┴────────────────┴─────────────┴─────────────┘
```

Per severity:

```
┌──────────┬─────────────┐
│ Severity │ All reports │
├──────────┼─────────────┤
│ HIGH     │ 9           │
├──────────┼─────────────┤
│ LOW      │ 1           │
├──────────┼─────────────┤
│ Total    │ 10          │
└──────────┴─────────────┘
```

And a list of **every** known checker with its guideline, whether it was
enabled in all selected runs, and closed/outstanding counts:

```
┌──────────────────────────┬───────────┬─────────────┬──────────────────────────────┬────────────────┬─────────────────────┐
│ Checker Name             │ guideline │ Severity    │ Enabled in all selected runs │ Closed Reports │ Outstanding Reports │
├──────────────────────────┼───────────┼─────────────┼──────────────────────────────┼────────────────┼─────────────────────┤
│ __FAKE__                 │           │ UNSPECIFIED │ True                         │ 0              │ 0                   │
│ NOT FOUND                │           │ UNSPECIFIED │ True                         │ 0              │ 0                   │
│ abseil-cleanup-ctad      │           │ STYLE       │ False                        │ 0              │ 0                   │
│ ...                      │           │             │                              │                │                     │
```

With the demo setup (Cppcheck installed) this third table is about 2060 lines long.

⚠️ Observed quirks:
- The "False positive" column counts reports that the "All reports" column
  (default review-status filter) excludes, so the columns do not add up
  (`core.DivideZero`: 2 unreviewed + 2 false positive, all = 2).
- The third table includes placeholder entries `__FAKE__` and `NOT FOUND`.

## All runs, with filters ✅

```sh
CodeChecker cmd sum --all --checker-name "core.*" -o json --url localhost:8555/Default
CodeChecker cmd sum -n demo --component maincomp --url localhost:8555/Default
CodeChecker cmd sum --all --severity high --url localhost:8555/Default
```

```json
[{"checker": "core.DivideZero", "severity": "HIGH", "reports": 2, "unreviewed": 2,
  "confirmed": 0, "false_positive": 2, "intentional": 0},
 {"checker": "core.NullDereference", "severity": "HIGH", "reports": 1, "unreviewed": 1,
  "confirmed": 0, "false_positive": 0, "intentional": 0}]
```

The JSON output contains only the per-checker table.

⚠️ The help text says several run names for `-n` are separated by `:`
(`run_1:run_2`). That does not work: `-n demo:multi` prints
`[WARNING] - No runs were found!`. Use spaces: `-n demo multi` ✅.

Exit code: `0`.
