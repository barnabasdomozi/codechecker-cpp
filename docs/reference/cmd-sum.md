# Reference: `CodeChecker cmd sum`

Usage: [../usage/cmd-sum.md](../usage/cmd-sum.md)

Python source: `web/client/codechecker_client/cli/cmd.py` (`__register_sum`),
`web/client/codechecker_client/cmd_line_client.py`
(`handle_list_result_types`),
`web/server/codechecker_server/api/report_server.py` (`getCheckerCounts`,
`getSeverityCounts`, `getCheckerStatusVerificationDetails`,
`getCheckerLabels`).

## 1. Command line

```
CodeChecker cmd sum (-n RUN_NAME... | -a/--all) [filter options]
                    [--filter-preset NAME]
                    [-o {plaintext,rows,table,csv,json}] [--url PRODUCT_URL]
```

- `-n` and `-a` are mutually exclusive and one is required.
- `-n` takes space-separated patterns (`*` wildcard). The help text describes
  a `:`-separated list; that is not implemented — `demo:multi` is one pattern
  matching nothing.
- Filter options as in [cmd-results.md](cmd-results.md) §2, but the
  `--uniqueing` default is `on`. (Implementation note: the code sets this by
  mutating the shared defaults dictionary after `results` and `diff` were
  registered; it has no other visible effect.)

## 2. Flow

1. Logger to stderr unless `-o table`; deprecation warnings
   ([local-diff.md](local-diff.md) §2); client
   ([server-api.md](server-api.md) "Client").
2. `--component` names must exist ([cmd-results.md](cmd-results.md) §2).
3. Run ids: `-a` → null (all runs). `-n` →
   `getRunData(RunFilter(names))` (paged); none →
   WARNING `No runs were found!`, exit 1.
4. `F` = report filter ([cmd-results.md](cmd-results.md) §2, or the preset).
5. Server calls (each `getCheckerCounts` with limit 500, offset 0 — a
   single page, so at most 500 checkers):
   - `all = getCheckerCounts(runIds, F, null, 500, 0)`;
   - for each of `UNREVIEWED`, `CONFIRMED`, `FALSE_POSITIVE`, `INTENTIONAL`:
     `getCheckerCounts(runIds, F with reviewStatus = [that status], ...)` →
     map checker name → count. Because `reviewStatus` is **replaced**, these
     columns ignore the `--review-status` filter, while "All reports" uses it
     (default `unreviewed confirmed`). Hence "False positive" and
     "Intentional" can be non-zero while not included in "All reports".
   - `sev = getSeverityCounts(runIds, F, null)`;
   - `details = getCheckerStatusVerificationDetails(runIds, F)`;
   - `labels = getCheckerLabels([Checker(analyzerName, checkerName) for each detail])`.
6. Guidelines per checker: from its labels, every label `guideline:<g>` gives
   a guideline `g`, whose rules are the values of the labels `<g>:<rule>`.
   Formatted as `"<g>: <rule>, <rule>"`, guidelines joined with `";  "` (two
   spaces); none → empty cell.
7. Output (§4). Exit 0.

## 3. Server calls

### `getCheckerCounts(runIds, filter, cmpData, limit, offset)`

Rows = reports matching the filter ([cmd-results.md](cmd-results.md) §3;
empty `runIds` = all runs) joined with their checker.
- non-unique: `GROUP BY (checker_name, severity)`, count rows;
- unique (`isUnique`): first group by hash (taking `MAX(checker_name)`,
  `MAX(severity)` per hash), then group by checker name, `MAX(severity)`,
  count hashes;
- ordered by checker name; `limit` (≤ 500) / `offset` applied.
Returns `[{name, severity (enum int), count}]`.

### `getSeverityCounts(runIds, filter, cmpData)`

Same rows; map severity → count (unique mode: per hash `MAX(severity)`, then
count hashes per severity).

### `getCheckerStatusVerificationDetails(runIds, filter)`

Returns a map `checker id → {checkerName, analyzerName, severity, enabled
[run ids], disabled [run ids], unknown [run ids], closed, outstanding}` for
**every row of the `checkers` table** — every checker ever stored in the
product (each store records all checkers known to the analyzers, see
[store.md](store.md)), plus the two placeholder rows that a schema
migration inserts into every product database: `__FAKE__/__FAKE__` and
`UNKNOWN/NOT FOUND` ([server-database.md](server-database.md)).

- For each selected run (all runs if `runIds` is empty), take its **latest**
  history entry, that entry's analysis info, and the analysis info's checker
  set items (checker id, enabled flag). A checker gets the run id in
  `enabled` or `disabled` accordingly. Runs whose latest history has no
  checker set (stored by old clients) are put in `unknown` of every checker.
- `outstanding` / `closed`: number of reports (distinct hashes in unique
  mode) of that checker in the selected runs with `fixed_at IS NULL` /
  `IS NOT NULL`. **No other filter field is applied** here.

### `getCheckerLabels(checkers)`

For each `(analyzerName, checkerId)`: the server's checker label database
([checker-labels.md](checker-labels.md)) entries as `"key:value"` strings;
analyzer `unknown` (any case) or empty → look up by checker name only.

## 4. Output

### JSON (`-o json`)

Only the per-checker statistics, one line:

```json
[{"checker": "<name>", "severity": "<SEVERITY NAME>", "reports": <all>,
  "unreviewed": n, "confirmed": n, "false_positive": n, "intentional": n}, ...]
```

Order: `all` sorted by severity value descending (stable, so ties keep the
server's name order).

### Tables

Three tables printed one after the other, each by the two-dimensional
formatter ([common-infrastructure.md](common-infrastructure.md) §7):

1. `Checker, Severity, Unreviewed, Confirmed, False positive, Intentional, All reports`
   — one row per checker as in the JSON, then a footer row
   `Total, -, <sums>` (rendered in "separate footer" mode: in
   `table`/`plaintext` a rule is drawn between **every** row when there are
   at least two rows).
2. `Severity, All reports` — rows from `sev` sorted by severity value
   descending, names as enum names, then `Total, <sum>` as footer.
3. `Checker Name, guideline, Severity, Enabled in all selected runs, Closed Reports, Outstanding Reports`
   — one row per checker of `details` (map order = `checkers` table id
   order): name, guidelines (§2 step 6), severity name, `True` if the
   checker's `disabled` list is empty (so checkers never seen in any run,
   and the placeholders, show `True`), closed, outstanding. No footer.
