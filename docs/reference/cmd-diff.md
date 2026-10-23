# Reference: `CodeChecker cmd diff` with server runs

Usage: [../usage/cmd-diff.md](../usage/cmd-diff.md)

Python source: `web/client/codechecker_client/cmd_line_client.py`
(`process_run_args`, `get_run_tag`, `parse_report_filter`,
`get_diff_remote_runs`, `get_diff_remote_run_local_dir`,
`get_diff_local_dir_remote_run`, `get_diff_base_results`,
`get_suppressed_reports`, `convert_report_data_to_report`),
`web/server/codechecker_server/api/report_server.py`
(`getDiffResultsHash`, `getRunResults`, `process_cmp_data_filter`,
`get_diff_bug_id_query`, `filter_open_reports_in_tags`,
`getReviewStatusRules`).

Options, side classification, the local–local mode, the output stage and
exit codes: [local-diff.md](local-diff.md). Server calls and the meaning of
the database columns: [server-api.md](server-api.md) and
[server-database.md](server-database.md). Filter semantics on the server:
[cmd-results.md](cmd-results.md) §3.

Terms used below:
- **open report** (server): a `reports` row with `fixed_at IS NULL`. A report
  gets `fixed_at` when it becomes resolved / off / unavailable at a later
  store, or when it gets a `false_positive` / `intentional` review status
  (from a source comment at store time, [store.md](store.md); or from a
  review status rule, [cmd-suppress.md](cmd-suppress.md)).
- **open at tag T**: `detected_at <= T.time AND (fixed_at IS NULL OR fixed_at > T.time)`,
  where `T` is a `run_histories` row.
- **hash**: the report hash (`bug_id` column on the server,
  [report-hash.md](report-hash.md)).

## 1. Resolving run arguments (`process_run_args`)

For each value `V` of a side:

1. Split `V` on `:` characters that are not preceded by `\`; in every part
   replace `\:` with `:`. Part 0 is the run name pattern, part 1 (if present
   and non-empty) the tag name; further parts are ignored.
2. `getRunData(RunFilter(names=[pattern]))`, paged with
   `limit = MAX_QUERY_SIZE (500)` until a page is shorter than the limit.
   The server matches the name case-insensitively with `*` as the only
   wildcard (`%`, `_` and `\` are escaped). No match →
   WARNING `No run names match the given pattern: <V>` and exit 1.
3. All matching run ids and names are added to the side.
4. If a tag name is given: `getRunHistory(<ids of this pattern>, 500, null,
   RunHistoryFilter(tagNames=[tag]))`; the **first** returned history entry
   (the server orders by store time, newest first) is used; its id is added
   to the side's tag ids. Tag names are matched like run names (`*`
   wildcard, case-insensitive). An unknown tag is **silently ignored**: that
   side then means the runs' current state.

Result per side: run ids, run names, tag ids.

## 2. Remote–remote (`-b RUN... -n RUN...`)

### Client

1. Report filter: `parse_report_filter` (see §5).
2. Base: run ids/tags from §1; `report_filter.runTag = base tag ids` (this
   **replaces** any `--tag` filter).
3. `CompareData{diffType, runIds = new run ids, runTag = new tag ids}`.
4. If either side has tags: if the detection status filter is neither the
   default (`[NEW, REOPENED, UNRESOLVED]`) nor empty, WARNING
   `--detection-status is ignored when comparing tags, showing reports regardless of detection status.`;
   then the detection status filter is cleared.
5. `getRunResults(base run ids, 500, offset, [FILENAME ASC], filter,
   cmpData, details=true)`, paged until a page is shorter than 500.
6. Convert to reports (§6), print ([local-diff.md](local-diff.md) §6,
   without leftover hashes), INFO
   `Compared multiple remote runs <base args> (matching: <base names>) and <new args> (matching: <new names>)`.

### Server (`process_cmp_data_filter`)

When `cmpData` has run ids, tags or an open-reports date, two hash sets are
built:

```
hashes(runIds, tagIds, openDate) =
    DISTINCT bug_id FROM reports
    WHERE run_id IN runIds                       (if runIds)
      AND fixed_at IS NULL                       (if runIds and no tagIds and no openDate)
      AND <open at tag T> for T IN tagIds        (if tagIds; join run_histories on run_id)
      AND <open at openDate>                     (if openDate)

base = hashes(base run ids, filter.runTag, filter.openReportsDate)
new  = hashes(cmp.runIds,   cmp.runTag,    cmp.openReportsDate)
```

and the report rows returned are:

| Diff type | Rows |
|---|---|
| NEW | `bug_id ∈ new − base` and `run_id ∈ new runs` |
| RESOLVED | `bug_id ∈ base − new` and `run_id ∈ base runs` |
| UNRESOLVED | `bug_id ∈ base ∩ new` and `run_id ∈ new runs` |

("new runs"/"base runs" = the run ids, restricted to runs having one of the
tags if tags are given.) All other report filter fields are then applied to
these rows (AND), including the default `--review-status unreviewed
confirmed` and `--detection-status new reopened unresolved` (the latter is
cleared by the client when tags are used). So a `false_positive` report
never shows up; since it also has `fixed_at` set it is not in the hash sets,
which makes the matching report on the other side appear as new/resolved.

The rows are not deduplicated by hash: the same hash in two runs of one side
gives two rows.

## 3. Remote–local (`-b RUN... -n DIR/BASELINE...`)

1. Read the local side ([local-diff.md](local-diff.md) §5.2) with the
   report filter's offline-applicable fields.
2. `suppressed_in_code` = hashes of local reports whose review status is not
   `unreviewed`. (The code means "not unreviewed and not in the review status
   filter", but compares a status object with strings, so the second
   condition is always true; **confirmed** local reports count as
   suppressed.)
3. Resolve the base runs (§1).
4. `closed` = hashes of server review status **rules** with status
   `false_positive` or `intentional` whose hash matches one of the local
   hashes (`getReviewStatusRules(ReviewStatusRuleFilter{reportHashes,
   reviewStatuses=[FALSE_POSITIVE, INTENTIONAL]})`; rule hashes are matched
   with `ILIKE`, `*` wildcard; the server processes the hash list in chunks of
   999). If the local hash list is empty, the hash condition is dropped
   and all such rules are returned.
5. Keep only local reports with status not `false_positive`/`intentional`
   and (hash not in `closed` or status is `confirmed`).
   `local_hashes` = their hashes ∪ (baseline file hashes − `closed`).
6. `remote = getDiffResultsHash(base run ids, local_hashes, diffType, null, base tag ids)`
   (§7).
7. `remote` empty → no reports, no leftover.
8. NEW / UNRESOLVED: reports = local reports whose hash ∈ `remote` and ∉
   `suppressed_in_code`; leftover = `remote` − hashes of all kept local
   reports (= hashes that came from baseline files).
9. RESOLVED: reports = `get_diff_base_results(remote)` (§4.1), converted
   (§6); no leftover.
10. Print; INFO
    `Compared remote run(s) <base args> (matching: <names>) and local files / report directory(s) <new paths>`.

## 4. Local–remote (`-b DIR/BASELINE... -n RUN...`)

Steps 1–5 of §3 with the local side as base and the new runs as remote.
Then:

| Diff type | Algorithm |
|---|---|
| NEW | `remote = getDiffResultsHash(new ids, local_hashes, RESOLVED, null, new tags)` (remote hashes missing locally); reports = `get_diff_base_results(remote)`; no leftover |
| UNRESOLVED | `remote = getDiffResultsHash(new ids, local_hashes, UNRESOLVED, null, new tags)`; reports = local reports with hash ∈ `remote` and ∉ `suppressed_in_code`; `missing` = (`local_hashes` − kept local report hashes) ∩ `remote` (baseline-only hashes present on the server); if non-empty, fetch `get_diff_base_results(missing)`, append those server reports and remove their hashes from `missing`; leftover = `missing` |
| RESOLVED | `remote = getDiffResultsHash(new ids, local_hashes, UNRESOLVED, null, new tags)`; reports = local reports with hash ∉ `remote`; leftover = (`local_hashes` − kept local report hashes) − `remote` |

INFO
`Compared local files / report directory(s) <base paths> and remote run(s) <new args> (matching: <names>).`

### 4.1 `get_diff_base_results(hashes)`

Fetch server reports for a hash list:
- `filter.reportHash = hashes + suppressed_in_code` (replaces any
  `--report-hash`; so local reports that are suppressed/confirmed are fetched
  from the server too);
- `filter.reviewStatus = [UNREVIEWED, CONFIRMED]` (replaces
  `--review-status`);
- all other filter fields stay;
- `getRunResults(run ids, 500, offset, [FILENAME ASC], filter, null,
  details=true)` paged as above. An empty hash list means "no reports" on the
  server (`reportHash == []` short-circuits to an empty result).

## 5. Report filter for server modes (`parse_report_filter`)

- `--filter-preset NAME` given:
  - conflicting options = those of `severity detection_status review_status
    checker_name file_path bug_path_length checker_msg analyzer_name component
    report_hash open_reports_date detected_at fixed_at detected_before
    detected_after fixed_before fixed_after tag report_status` that are
    present and differ from their default (§1 of
    [local-diff.md](local-diff.md)); any →
    `Cannot combine --filter-preset with other filter arguments (--opt, ...). Either use a preset or specify filters on the command line, not both.`,
    exit 1;
  - INFO `Loading filter preset '<name>'...`; `listFilterPreset()`; the first
    preset with exactly that name is used as the whole `ReportFilter`; none →
    ERROR `Filter preset '<name>' not found!`, INFO
    `Use 'CodeChecker cmd filter-preset list' to see available presets.`,
    exit 1.
- otherwise: the offline filter ([local-diff.md](local-diff.md) §5.1, all
  fields), plus `--tag T...` → `getRunHistory(null, 500, null,
  RunHistoryFilter(tagNames=T))` and `runTag` = the ids of **all** returned
  histories (of any run).

The server applies the fields as described in
[cmd-results.md](cmd-results.md) §3.

## 6. Converting server reports (`convert_report_data_to_report`)

For each `ReportData` (with `details`):
- source lines: unless `html` is among the formats, one
  `getLinesInSourceFileContents([{fileId, lines}], BASE64)` call for all
  (file, line) pairs; the decoded line plus a newline becomes the report's
  source line;
- files: one `File(path = checkedFile, id = fileId)` per file id; with `html`,
  `getSourceFileData(fileId, true, BASE64)` provides the content;
- the report gets: file, line, column, message, checker name, severity (enum
  name), report hash (`bugHash`), analyzer name, review status, detection
  status, bug path events / macro expansions / notes from `details`
  (each event's file from the same cache), annotations;
- no changed files, no source code comments.

Note that `checkedFile` is the path **as stored** (possibly trimmed with
`store --trim-path-prefix`), which is why server reports often print with
relative paths.

## 7. `getDiffResultsHash(runIds, hashes, diffType, skipStatuses, tagIds)`

Requires view permission. `skipStatuses` defaults to `[RESOLVED, OFF,
UNAVAILABLE]`. For clients with API version ≥ 6.50 (all current clients):

| Diff type | Returns |
|---|---|
| NEW | `hashes` minus `{bug_id of rows in runIds, detection_status ∉ skip, fixed_at IS NULL, open at each tag}`; `[]` if `hashes` is empty |
| RESOLVED | `bug_id` of rows in `runIds` (open at the tags if given) with `bug_id ∉ hashes OR fixed_at IS NOT NULL` (may contain duplicates) |
| UNRESOLVED | `bug_id` of rows in `runIds` (open at the tags) with `bug_id ∈ hashes`, `detection_status ∉ skip`, `fixed_at IS NULL` |

For older clients the `fixed_at` conditions are omitted and "open at tag" is
only `detected_at <= tag time`. On SQLite the NEW query is built in chunks of
`SQLITE_MAX_COMPOUND_SELECT` (500) hashes, each as `UNION ALL` of constant
selects `EXCEPT` the base query; on PostgreSQL with `unnest(array)`.

## 8. Fixit pipe

`cmd diff -o json` produces the local JSON format
(`{"version": 1, "reports": [...]}`, [parse-export.md](parse-export.md)
"JSON"); `fixit` expects `checkedFile` in each report and fails
([fixit.md](fixit.md) §3).
