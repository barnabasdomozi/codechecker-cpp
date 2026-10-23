# Reference: `CodeChecker cmd runs` and `cmd history`

Usage: [../usage/cmd-runs-history.md](../usage/cmd-runs-history.md)

Python source: `web/client/codechecker_client/cli/cmd.py`
(`__register_runs`, `__register_run_histories`, `__add_common_arguments`),
`web/client/codechecker_client/cmd_line_client.py` (`handle_list_runs`,
`handle_list_run_histories`, `process_run_filter_conditions`,
`get_run_data`), `web/server/codechecker_server/api/report_server.py`
(`getRunData`, `process_run_filter`, `sort_run_data_query`,
`filter_unresolved_reports`, `get_analysis_statistics_query`,
`getRunHistory`, `process_run_history_filter`, `getAnalysisInfo`,
`getAnalysisStatistics`).

Common to both commands: `--url PRODUCT_URL` (default
`localhost:8001/Default`), `-o {plaintext,rows,table,csv,json}` (default
`plaintext`), `--verbose`. Logging goes to stderr unless `-o table`. The
client setup and authentication are in [server-api.md](server-api.md)
"Client". Exit 0 on success; connection/authentication/API errors exit 1.

## 1. `cmd runs`

```
CodeChecker cmd runs [-n [NAME ...] | --all-before-run RUN | --all-after-run RUN
                      | --all-before-time TS | --all-after-time TS]
                     [--sort {name,unresolved_reports,date,duration,codechecker_version}]
                     [--order {asc,desc}] [--details] [-o FORMAT] [--url URL]
```

The five selection options are mutually exclusive (argparse). Defaults:
`--sort date`, `--order desc`. `TS` = `year:month:day[:h[:m[:s]]]`, local
time → Unix timestamp (see [cmd-results.md](cmd-results.md) §2).

### 1.1 Run filter

| Option | `RunFilter` |
|---|---|
| `-n NAME...` | `names = NAME...`, `exactMatch = false` (`-n` without values → empty list → all runs) |
| `--all-after-run R` | `afterRun = R` |
| `--all-before-run R` | `beforeRun = R` |
| `--all-after-time TS` | `afterTime` |
| `--all-before-time TS` | `beforeTime` |

Server semantics (`process_run_filter`):
- `names` (not exact): `run.name ILIKE pattern` for any pattern, where the
  pattern has `\`, `%`, `_` escaped and `*` → `%` (so `*` is the only
  wildcard; case-insensitive). With `exactMatch`: `name IN names`.
- `beforeTime`/`afterTime`: `run.date < t` / `run.date > t`.
- `beforeRun`/`afterRun`: look up the run with exactly that name; if it
  exists, `run.date <` / `>` its date; if not, **no condition** (all runs).
- `run.date` is the time of the run's latest store (see [store.md](store.md)).

### 1.2 Server: `getRunData(filter, limit, offset, sortMode)`

Requires view permission; limit capped at 500. The client pages with limit
500 until a page is shorter.

For every matching run:

| Field | Value |
|---|---|
| `runId`, `name` | |
| `runDate` | `str(run.date)` (`YYYY-MM-DD HH:MM:SS.ffffff`) |
| `duration` | `run.duration` in seconds (`-1` while a store is in progress) |
| `resultCount` | count of the run's reports with detection status ∉ {resolved, off, unavailable} **and** review status ∉ {false_positive, intentional}; 0 if none |
| `detectionStatusCount` | map detection status enum → number of the run's reports |
| `versionTag`, `codeCheckerVersion`, `description` | from the run's **latest** history entry (highest id) |
| `analyzerStatistics` | map analyzer name → `{failed, successful}` from the latest history entry that has statistics |

Sorting: `NAME` → name; `UNRESOLVED_REPORTS` → `resultCount`; `DATE` →
`run.date`; `DURATION` → duration; `CC_VERSION` → latest history's
CodeChecker version; each ascending or descending. With no sort mode:
date descending.

### 1.3 Output

- `-o json` with `--details`: for each run additionally
  - `getAnalysisInfo(AnalysisInfoFilter(runId), 500, 0)`: collect, per
    analyzer, the names of the checkers with `enabled = true` over all
    returned analysis info entries;
  - `getAnalysisStatistics(runId, null)` replaces `analyzerStatistics`
    (`{analyzer: AnalyzerStatistics}` with more fields, see
    [server-api.md](server-api.md)); each entry gets
    `enabledCheckers` = the sorted list collected above (or `[]`).
- `-o json`: a list of single-key objects `[{"<run name>": <RunData>}, ...]`,
  serialised field by field (enums as integers), indented with 4 spaces, no
  trailing newline.
- Other formats: header
  `Name, Number of unresolved reports, Analyzer statistics, Storage date, Version tag, Duration, Description, CodeChecker version`;
  per run:
  - Number of unresolved reports: `resultCount`;
  - Analyzer statistics: `"<analyzer> (<failed+successful>/<successful>)"`
    joined with `", "` in map order;
  - Storage date: `runDate`;
  - Version tag / Description / CodeChecker version: value or empty;
  - Duration: `H:MM:SS` (Python `timedelta`; `D day(s), H:MM:SS` beyond 24 h)
    or `Not finished` if `duration` is `-1`.

  Rendered with the two-dimensional formatter
  ([common-infrastructure.md](common-infrastructure.md) §7). Without runs
  only the header is printed (exit 0).

`--details` without `-o json` has no effect.

## 2. `cmd history`

```
CodeChecker cmd history [-n [NAME ...]] [-o FORMAT] [--url URL]
```

1. If `-n` is given: `getRunData(RunFilter(names))` (paged) → run ids.
   Otherwise run ids = null.
2. `getRunHistory(runIds, 500, null, null)` — **one** call, so at most the
   500 newest entries are shown.
3. Server: history rows, `run_id IN runIds` only if the list is non-empty
   (so a `-n` pattern matching **no** run lists the history of **all**
   runs), newest first (`ORDER BY time DESC`). Each entry is a
   `RunHistoryData` with `id, runId, runName, versionTag, user, time (str),
   codeCheckerVersion, analyzerStatistics {analyzer: AnalyzerStatistics},
   description` set; `checkCommand` stays null, and in each
   `AnalyzerStatistics` only `failed` and `successful` are set (the other
   fields, e.g. `version`, `failedFilePaths`, are null).
4. Output:
   - `json`: the list of entries serialised field by field on one line;
   - others: header
     `Date, Run name, Version tag, User, CodeChecker version, Analyzer statistics, Description`;
     values: time, run name, tag or empty, user, version or empty, analyzer
     statistics formatted as in §1.3, description or empty.

`user` is the authenticated user name at store time, `Anonymous` on servers
without authentication.

## 3. Server: `getRunHistory` filter (used by other commands)

`RunHistoryFilter` fields:
- `tagNames`: `version_tag ILIKE pattern` (escaped like run names, `*`
  wildcard);
- `tagIds`: id in list;
- `stored.before/after`: `time <= before`, `time >= after`.

`getRunHistoryCount(runIds, filter)` returns the number of matching rows.
