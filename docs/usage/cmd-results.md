# List reports on a server (`cmd results`)

```
CodeChecker cmd results RUN_NAME [...] [--details] [filters] [--filter-preset NAME]
                        [-o {plaintext,rows,table,csv,json}] [--url PRODUCT_URL]
```

Run names can contain `*`, e.g. `"demo*"`. Several runs can be given.

## List the reports of a run ✅

```sh
CodeChecker cmd results demo --url localhost:8555/Default
```

```
┌───────────────┬──────────────────────┬──────────┬──────────────────────────────────────┬─────────────────┬───────────────┬───────────────┬──────────────────┐
│ File          │ Checker              │ Severity │ Message                              │ Bug path length │ Analyzer name │ Review status │ Detection status │
├───────────────┼──────────────────────┼──────────┼──────────────────────────────────────┼─────────────────┼───────────────┼───────────────┼──────────────────┤
│ main.c @ 11   │ core.DivideZero      │ HIGH     │ Division by zero                     │ 2               │ clangsa       │ UNREVIEWED    │ UNRESOLVED       │
│ main.c @ 21   │ core.NullDereference │ HIGH     │ Dereference of null pointer (loaded  │ 2               │ clangsa       │ UNREVIEWED    │ NEW              │
│               │                      │          │ from variable ...                    │                 │               │               │                  │
│ style.cpp @ 4 │ core.DivideZero      │ HIGH     │ Division by zero                     │ 2               │ clangsa       │ UNREVIEWED    │ NEW              │
│ main.c @ 25   │ clang-diagnostic-    │ HIGH     │ variable 'x' is uninitialized when   │ 2               │ clang-tidy    │ UNREVIEWED    │ UNRESOLVED       │
│               │ uninitialized        │          │ used here                            │                 │               │               │                  │
│ style.cpp @ 4 │ modernize-use-       │ LOW      │ use nullptr                          │ 1               │ clang-tidy    │ UNREVIEWED    │ NEW              │
│               │ nullptr              │          │                                      │                 │               │               │                  │
└───────────────┴──────────────────────┴──────────┴──────────────────────────────────────┴─────────────────┴───────────────┴───────────────┴──────────────────┘
```

(The run was stored twice, `v1` then `v2`, with `--trim-path-prefix` on the
second store; see [store.md](store.md).) Long messages are cut with `...` in
tables. By default only review statuses `unreviewed`/`confirmed` and detection
statuses `new`/`reopened`/`unresolved` are shown.

## Output formats ✅

```sh
CodeChecker cmd results demo --tag v1 --uniqueing on -o csv --url localhost:8555/Default
```

```
File,Checker,Severity,Message,Bug path length,Analyzer name,Review status,Detection status
main.c @ 11,core.DivideZero,HIGH,Division by zero,2,clangsa,UNREVIEWED,UNRESOLVED
main.c @ 25,clang-diagnostic-uninitialized,HIGH,variable 'x' is uninitialized when used here,2,clang-tidy,UNREVIEWED,UNRESOLVED
```

## Full details as JSON ✅

```sh
CodeChecker cmd results "demo*" --details -o json --url localhost:8555/Default
```

```json
[{"runId": 1, "checkerId": "core.DivideZero", "bugHash": "09296549497056a329a9ce22dda7d710",
  "checkedFile": "main.c", "checkerMsg": "Division by zero", "reportId": 6, "fileId": 4,
  "line": 11, "column": 12, "severity": 40,
  "reviewData": {"status": 0, "comment": "", "author": "Anonymous", "date": "...", "isInSource": false},
  "detectionStatus": 2, "detectedAt": "2026-10-23 23:39:43.597604", "fixedAt": null,
  "bugPathLength": 2,
  "details": {"pathEvents": [{"startLine": 10, "startCol": 3, "endLine": 10, "endCol": 7,
                              "msg": "'z' initialized to 0", "fileId": 4, "filePath": "main.c"}, ...],
              "executionPath": [...], "extendedData": [], "comments": []},
  "analyzerName": "clangsa", "annotations": {}, "blameInfo": {"commits": null, "blame": null},
  "runName": "demo"}, ...]
```

In JSON, enums are numbers (Thrift values): `severity` 40 = HIGH, 20 = LOW;
`reviewData.status` 0 = unreviewed, 2 = false positive; `detectionStatus`
0 = new, 2 = unresolved. `--details` adds `details` (bug path, notes,
comments) and git blame information if it was stored.

## Filters ✅

All filters below were run against the test server. The same filters work in
`cmd sum`, `cmd diff` and `cmd filter-preset new`.

| Filter | Example | Notes |
|---|---|---|
| `--severity` | `--severity high medium` | Case does not matter here |
| `--checker-name` | `--checker-name "core.*"` | `*` wildcards |
| `--checker-msg` | `--checker-msg "*zero*"` | `*` wildcards |
| `--analyzer-name` | `--analyzer-name clangsa "clang*"` | |
| `--file` | `--file "*main.c"` | Matches the stored path; `*` wildcards |
| `--component` | `--component maincomp` | See [cmd-components.md](cmd-components.md) |
| `--review-status` | `--review-status false_positive` | unreviewed, confirmed, false_positive, intentional |
| `--detection-status` | `--detection-status resolved` | new, resolved, unresolved, reopened, off, unavailable |
| `--report-status` | `--report-status outstanding` | outstanding, closed |
| `--report-hash` | `--report-hash 0929654949...` | |
| `--bug-path-length` | `--bug-path-length 2:` | `min:max`, `4:10`, `4:`, `:10` |
| `--tag` | `--tag v1` | Reports present in the store with that tag |
| `--uniqueing` | `--uniqueing on` | One row per bug hash even if found on several paths |
| `--detected-after` / `--detected-before` | `--detected-after 2020:01:01` | `year:month:day[:h:m:s]` |
| `--fixed-after` / `--fixed-before` | `--fixed-after 2020:01:01` | Combine with `--detection-status resolved` |
| `--outstanding-reports-date` (`--open-reports-date`) | `--outstanding-reports-date 2030:01:01` | Detected before and not fixed before the date |
| `--anywhere-on-report-path` | with `--file`/`--component` | Match if the bug path goes through the file, not only ends there |
| `--single-origin-report` | with `--component` | Bug path entirely inside the component |
| `--detected-at`, `--fixed-at` | | Deprecated |

Examples:

```sh
CodeChecker cmd results demo --severity high --checker-name "core.*" --file "*main.c" -o table --url localhost:8555/Default
CodeChecker cmd results demo --detection-status resolved --url localhost:8555/Default
CodeChecker cmd results demo --review-status false_positive --url localhost:8555/Default
CodeChecker cmd results demo --bug-path-length 2: --analyzer-name clangsa --checker-msg "*zero*" --detected-after 2020:01:01 --url localhost:8555/Default
```

```
│ main.c @ 21 (full path) │ clang-diagnostic-unused-variable │ MEDIUM │ unused variable 'unused' │ 1 │ clang-tidy │ UNREVIEWED │ RESOLVED │
```

```
│ style.cpp @ 11 │ core.DivideZero │ HIGH │ Division by zero │ 2 │ clangsa │ FALSE_POSITIVE │ UNRESOLVED │
│ style.cpp @ 17 │ core.DivideZero │ HIGH │ Division by zero │ 2 │ clangsa │ FALSE_POSITIVE │ UNRESOLVED │
```

Notes from testing:
- `--fixed-after/--fixed-before` with the default detection-status filter
  returns nothing, because fixed reports are `resolved` and the default
  excludes `resolved`.
- ⚠️ `--component maincomp --single-origin-report` returned nothing, even for
  reports whose whole bug path is inside `main.c`.
- An empty result prints an empty line and exits `0`.

## Saved filter presets ✅

```sh
CodeChecker cmd results demo --filter-preset core_high --url localhost:8555/Default
```

The preset's filters replace the defaults, including the review-status
default, so false positives may appear. See
[cmd-filter-preset.md](cmd-filter-preset.md).

Exit code: `0` (`1` on errors).
