# Reference: `CodeChecker cmd results` and the report filter

Usage: [../usage/cmd-results.md](../usage/cmd-results.md)

Python source: `web/client/codechecker_client/cli/cmd.py`
(`__register_results`, `__add_filtering_arguments`, `valid_time`),
`web/client/codechecker_client/cmd_line_client.py` (`handle_list_results`,
`parse_report_filter`, `parse_report_filter_offline`, `get_run_results`),
`web/client/codechecker_client/cmd_line.py` (`CmdLineOutputEncoder`),
`web/server/codechecker_server/api/report_server.py` (`getRunResults`,
`process_report_filter`, component helpers, `get_sort_map`,
`get_report_details`, `getBlameInfo`).

The report filter described here (§2, §3) is shared by `cmd results`,
`cmd diff` ([cmd-diff.md](cmd-diff.md)), `cmd sum` ([cmd-sum.md](cmd-sum.md))
and `cmd filter-preset new` ([cmd-filter-preset.md](cmd-filter-preset.md)).

## 1. Command line

```
CodeChecker cmd results RUN_NAME... [--details] [filter options]
                        [--filter-preset NAME]
                        [-o {plaintext,rows,table,csv,json}] [--url PRODUCT_URL]
                        [--verbose L]
```

- `RUN_NAME...` (≥ 1): run name patterns, `*` wildcard.
- `-o` default `plaintext` (single value).
- `--url` default `localhost:8001/Default` ([server-api.md](server-api.md)
  "Product URL").
- Logging goes to stderr unless `-o table`.

## 2. Filter options → `ReportFilter`

Defaults that are always present: `--review-status unreviewed confirmed`,
`--detection-status new reopened unresolved`, `--uniqueing off`,
`--anywhere-on-report-path` false, `--single-origin-report` false. Other
options are absent unless given. Options with `nargs='*'` may be given with no
value, which yields an empty list (= no filtering for that field).

| Option | `ReportFilter` field | Conversion |
|---|---|---|
| `--severity S...` | `severity` | upper-case → enum (`UNSPECIFIED`=0, `STYLE`=10, `LOW`=20, `MEDIUM`=30, `HIGH`=40, `CRITICAL`=50) |
| `--detection-status D...` | `detectionStatus` | upper-case → enum (`NEW`=0, `RESOLVED`, `UNRESOLVED`, `REOPENED`, `OFF`, `UNAVAILABLE`=5) |
| `--review-status R...` | `reviewStatus` | upper-case → enum (`UNREVIEWED`=0, `CONFIRMED`, `FALSE_POSITIVE`, `INTENTIONAL`=3) |
| `--report-status S...` | `reportStatus` | upper-case → enum (`OUTSTANDING`=0, `CLOSED`=1) |
| `--checker-name P...` | `checkerName` | as is |
| `--file P...` | `filepath` | as is |
| `--checker-msg P...` | `checkerMsg` | as is |
| `--analyzer-name P...` | `analyzerNames` | as is |
| `--component C...` | `componentNames` | as is; each must exist (`getSourceComponents([C])` non-empty) else `The following source components don't exist: <names>`, exit 1 |
| `--report-hash H...` | `reportHash` | as is |
| `--bug-path-length MIN:MAX` | `bugPathLength{min, max}` | split on `:`; a part is used only if it consists of digits (`"4:"`, `":10"`, `"4:10"`) |
| `--tag T...` | `runTag` | `getRunHistory(null, 500, null, RunHistoryFilter(tagNames=T))` → ids of all matching histories (tag names: `*` wildcard, case-insensitive); none → field unset |
| `--uniqueing on/off` | `isUnique` | `on` → true |
| `--outstanding-reports-date` / `--open-reports-date TS` | `openReportsDate` | timestamp |
| `--detected-at TS` (deprecated) | `firstDetectionDate` | timestamp |
| `--fixed-at TS` (deprecated) | `fixDate` | timestamp |
| `--detected-before/--detected-after TS` | `date.detected.before/after` | timestamp |
| `--fixed-before/--fixed-after TS` | `date.fixed.before/after` | timestamp |
| `--anywhere-on-report-path` | `fileMatchesAnyPoint` and `componentMatchesAnyPoint` | |
| `--single-origin-report` | `fullReportPathInComponent` | |

- Unknown enum names raise `KeyError` (traceback, exit 1); the later
  validation with the message `Invalid <type> value(s): ...` is unreachable.
- Timestamp `TS`: `year:month:day[:hour[:minute[:second]]]`, missing parts 0;
  an invalid date is an argparse error (exit 1). Converted as **local time**
  to an integer Unix timestamp (`mktime`).
- Warnings: `--anywhere-on-report-path` without `--file`/`--component` →
  `The flag --anywhere-on-report-path is meaningful only if --file or --component is used.`;
  `--single-origin-report` without `--component` →
  `The flag --single-origin-report is meaningful only if --component is used.`;
  `--detected-at`/`--fixed-at` → deprecation warnings
  ([local-diff.md](local-diff.md) §2).
- `--filter-preset NAME`: the preset's stored `ReportFilter` replaces the
  whole filter, defaults included; combining it with non-default filter
  options is an error ([cmd-diff.md](cmd-diff.md) §5).

## 3. Server-side filter semantics (`process_report_filter`)

All conditions are combined with AND. "LIKE pattern" means: `*` in the value
is replaced by `%` and the value is used in a case-insensitive `ILIKE`; `%`
and `_` in the value are **not** escaped and act as SQL wildcards. Patterns
are matched against the whole column value.

| Field | Condition |
|---|---|
| `reportHash == []` (empty list, not unset) | no rows at all |
| `filepath` | `fileMatchesAnyPoint` false: the report's file path ILIKE any pattern. true: the report's file **or** any file of its bug path (events, notes, macros — the `report_path_data_files` association) matches |
| `checkerMsg` | message ILIKE any pattern |
| `analyzerNames` | checker's analyzer name ILIKE any |
| `checkerName` | checker name ILIKE any |
| `severity` | checker's severity in the list |
| `runName` | run name ILIKE any (used by the web UI) |
| `reportHash` | values containing `*` → hash ILIKE value; others → exact `IN` list; OR of both |
| `cleanupPlanNames` | hash is in a cleanup plan with one of the names |
| `reportStatus` | `outstanding` = review status ∈ {unreviewed, confirmed} AND detection status ∈ {new, unresolved, reopened}; `closed` = NOT outstanding; OR of the requested ones |
| `detectionStatus` | in the list (lower-case strings in the DB) |
| `reviewStatus` | in the list |
| `firstDetectionDate` | `detected_at >= date` |
| `fixDate` | `detected_at < date` (sic) |
| `date.detected.before/after` | `detected_at <= before`, `detected_at >= after` |
| `date.fixed.before/after` | `fixed_at <= before`, `fixed_at >= after` (rows with `fixed_at NULL` never match) |
| `openReportsDate` | `detected_at <= date AND (fixed_at IS NULL OR fixed_at > date)` (applied through the compare logic, see below) |
| `runHistoryTag` (deprecated) | per date string `'%Y-%m-%d %H:%M:%S.%f'`: open at that date (`fixed_at >= date`) |
| `componentNames` | see §3.1 |
| `bugPathLength` | `path_length >= min`, `path_length <= max` |
| `annotations` | see [server-api.md](server-api.md); not settable from the CLI |

Dates in the filter are Unix timestamps converted with the server's local
time zone.

Run restriction and tags (`process_cmp_data_filter` without compare data):
if run ids or `runTag`/`openReportsDate` are given, `run_id IN runIds`, and
for tags: join `run_histories` on the run, history id in `runTag` and the
report open at the history time (`detected_at <= time AND (fixed_at IS NULL OR fixed_at > time)`).
So `--tag v1` shows the reports that were open when `v1` was stored.

### 3.1 Components

A component is a list of lines `+<pattern>` / `-<pattern>`
([cmd-components.md](cmd-components.md)). The server keeps a
`source_component_files` table (component name → file id) that is filled in
two places, with **different** matching rules:

- when a component is added or updated (`addSourceComponent`): its rows are
  deleted and recomputed over all stored files with SQL `LIKE`, `*` → `%`
  (`_`/`%` are wildcards too; case-sensitive on PostgreSQL, ASCII
  case-insensitive on SQLite): files matching any `+` pattern minus files
  matching any `-` pattern; with only `-` patterns, all files except the
  matching ones; with no patterns, nothing;
- when a store inserts a **new** `files` row: the new file is tested against
  every component with shell-glob matching (`fnmatch`: case-sensitive, `*`
  also matches `/`, `?` and `[...]` are special), with the same
  include/skip logic.

A port should use one rule; the closest compatible choice is "`*` matches
any sequence, everything else literal".

- default: the report's file is one of the component's files;
- `componentMatchesAnyPoint`: the report's file or any bug path file is one;
- `fullReportPathInComponent`: intended "report file and all bug path files
  are in the component". The implementation (known issue) computes
  `(reports whose file is in C) ∪ (reports with a path file in C)` minus
  `(reports for which some file of C differs from the report's file)` minus
  `(reports with a path entry whose file differs from some file of C)`, which
  removes every report as soon as the component has two or more files.
  A port should implement the intended semantics.
- The special name `Other` (`GEN_OTHER_COMPONENT_NAME`) means "files in no
  component"; if no component exists it adds no condition.

## 4. Flow (`handle_list_results`)

1. Deprecation warnings; create the client ([server-api.md](server-api.md)
   "Client").
2. Check `--component` names exist (§2).
3. `getRunData(RunFilter(names=RUN_NAMES))` (paged by 500, no sort). No
   runs → WARNING `No runs were found!`, exit 1.
4. Build the filter (§2).
5. `details = --details AND -o json`.
6. `getRunResults(run ids, 500, offset, sort = null, filter, null, details)`
   paged until a page has fewer than 500 rows.
7. Output (§5). Exit 0.

## 5. Output

### JSON

Each `ReportData` is serialised as a JSON object of its fields (enum values
as **integers**, unset fields as `null`), with an added `runName`. The list
is printed on one line (`json.dumps` defaults). Fields:

```
runId, checkerId, bugHash, checkedFile, checkerMsg, reportId, fileId, line,
column, severity, reviewData {status, comment, author, date, isInSource},
detectionStatus, detectedAt, fixedAt, bugPathLength,
details {pathEvents[{startLine,startCol,endLine,endCol,msg,fileId,filePath}],
         executionPath[{startLine,startCol,endLine,endCol,fileId,filePath}],
         extendedData[{type,startLine,startCol,endLine,endCol,message,fileId,filePath}],
         comments[{id,author,message,createdAt,kind}]} | null,
analyzerName, annotations {key: value}, blameInfo {commits, blame} | null,
runName
```

(Field order follows the Thrift struct field order of `ReportData`.)

### Tables (`plaintext`, `rows`, `table`, `csv`)

Header:
`File, Checker, Severity, Message, Bug path length, Analyzer name, Review status, Detection status`.
Per report:
- File: `checkedFile + " @ " + line`;
- Severity / Review status / Detection status: enum **names**
  (`HIGH`, `FALSE_POSITIVE`, `UNRESOLVED`; `N/A` if detection status unset);
- Message: whitespace runs collapsed to one space; longer than 50 characters
  → first 50 + `...`.

Rendered with the two-dimensional formatter
([common-infrastructure.md](common-infrastructure.md) §7). With no reports
the header is empty and an empty line is printed. (A variant with commit
columns exists in the code but is unreachable, because details are only
requested for JSON.)

## 6. Server: `getRunResults(runIds, limit, offset, sortModes, filter, cmpData, getDetails)`

- Requires view permission; `limit` capped at 500 (0/null → 500, larger →
  500 with a server warning).
- Default sort: `SEVERITY DESC`. Sort keys: `FILENAME` → file path, then
  line; `CHECKER_NAME`; `SEVERITY`; `REVIEW_STATUS`; `DETECTION_STATUS`;
  `BUG_PATH_LENGTH`; `TIMESTAMP`/`TESTCASE`/`CHRONOLOGICAL_ORDER` → the
  annotation of that name. A final `ORDER BY report.id` keeps paging stable.
- Annotation columns: every distinct `report_annotations.key` becomes a
  column `annotation_<key>` (pivot with `MAX(CASE ...)`).
- **Unique mode** (`isUnique`): rows are grouped by hash; for each hash only
  the row with the highest report id is returned. `checkedFile` is then the
  file's **base name**, `FILENAME` sorts by base name, `DETECTION_STATUS`
  sorting is ignored, and `fixedAt` is the string `"None"` when unset.
- Non-unique mode: `checkedFile` = full stored path; `fixedAt` null when
  unset; annotation values as strings.
- `getDetails`: for each report, `details` (path events, execution path,
  extended data = notes/macros/fixits, comments newest first with system
  comments rendered from templates, HTML-escaped) and `blameInfo` filtered to
  the report line: only blame ranges with `startLine <= line <= endLine` and
  the commit of the first such range.
