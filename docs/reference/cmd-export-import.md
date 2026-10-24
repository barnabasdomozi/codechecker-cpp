# Reference: `CodeChecker cmd export` and `cmd import`

Usage: [../usage/cmd-export-import.md](../usage/cmd-export-import.md)

Python source: `web/client/codechecker_client/cli/cmd.py`
(`__register_export`, `__register_importer`),
`web/client/codechecker_client/cmd_line_client.py` (`handle_export`,
`handle_import`, `process_run_filter_conditions`, `get_run_data`),
`web/client/codechecker_client/cmd_line.py` (`CmdLineOutputEncoder`),
`web/server/codechecker_server/api/report_server.py` (`exportData`,
`importData`, `_setReviewStatus`, `create_review_data`).

## 1. `cmd export`

```
CodeChecker cmd export [-n RUN_NAME [RUN_NAME ...]] [--url PRODUCT_URL]
```

1. Logging to **stderr**.
2. Run filter: `-n` → `RunFilter(names, exactMatch=false)` (case-insensitive
   patterns, `*` wildcard); otherwise an empty `RunFilter` (all runs).
3. `getRunData(filter)` (paged); no runs → WARNING `No runs found`, exit 1.
4. `exportData(filter)`; print the result as JSON on one line (Thrift
   struct fields as object keys, enums as integers). Exit 0.

Output:

```json
{"comments": {"<report hash>": [CommentData, ...]},
 "reviewData": {"<report hash>": ReviewData}}
```

- `CommentData`: `{"id", "author", "message", "createdAt", "kind"}`;
  `message` is HTML-unescaped, `kind` 0 = user, 1 = system, `createdAt` =
  `str(datetime)` (`YYYY-MM-DD HH:MM:SS.ffffff`, but **without** the
  fraction when it is zero).
- `ReviewData`: `{"status", "comment", "author", "date", "isInSource"}`;
  `status` 0 unreviewed, 1 confirmed, 2 false positive, 3 intentional;
  `date` as above.

### Server `exportData(runFilter)` (needs view permission)

- Comments: every comment joined to the reports with its hash (outer join),
  restricted to reports of the matching runs; newest first (`created_at`,
  then id, descending); grouped by the report's hash. Each comment appears
  once per hash (verified).
- Review data: every report of the matching runs whose review status is
  not `unreviewed`, ordered by `review_status_date`; one entry per hash —
  the **last** one in that order wins. Source-code-comment statuses are
  included (`isInSource: true`).

## 2. `cmd import`

```
CodeChecker cmd import -i/--import JSON_FILE [--url PRODUCT_URL]
```

1. Load the file; missing, invalid or empty → ERROR `Failed to import
   data!`, exit 1. A file without `comments` or `reviewData` keys crashes
   (`KeyError`, exit 1).
2. Build `ExportData`: comments `{id, author, message, createdAt, kind}`,
   review data `{status, comment, author, date}` (`isInSource` is dropped).
3. `importData(data)`; false → ERROR `Failed to import data!`, exit 1; true →
   INFO `Import successful!`, exit 0.

### Server `importData(exportData)` (needs `PRODUCT_ADMIN`)

One transaction:

1. **Comments**: for every hash and comment: parse `createdAt` with
   `%Y-%m-%d %H:%M:%S.%f` (⚠️ a date without fraction, as exported for
   whole-second times, fails the whole import with `time data '<d>' does
   not match format '%Y-%m-%d %H:%M:%S.%f'` — verified); skip it if a stored
   comment of that hash has the same `created_at`, `kind` and message;
   otherwise insert it with the imported author, kind and date. Comments are
   not tied to runs; they show up for any report with the hash.
2. **Review statuses**: for every hash: skip if a review status rule exists
   whose `str(date)` equals the imported `date`; otherwise
   `_setReviewStatus(hash, status, comment, date)`:
   - old = existing rule (or `unreviewed`, no message); if
     `(old status, old message) == (new status, new message bytes)` →
     nothing changes;
   - store the rule `review_statuses(hash, status, author = the importing
     user — not the imported author, message, date = imported date)`;
   - add a system comment by the importing user at that date:
     `rev_st_changed_msg <Old> <New> <shell-quoted message>` or, without
     message, `rev_st_changed <Old> <New>` (`Old`/`New` = status names with
     the first letter upper-case, e.g. `False_positive`);
   - reports with this hash whose status is **not** from a source comment:
     - new status FP/intentional: set `fixed_at = date` on those that are
       open (detection `new`/`unresolved`/`reopened`) and not already
       FP/intentional;
     - otherwise: clear `fixed_at` on those that are open and were
       FP/intentional;
     - set `review_status`, `review_status_author`, `review_status_message`,
       `review_status_date` from the rule.
3. Commit, return true.

So an import turns every exported review status — including those that
came from source-code comments — into a review status rule on the target
product. Future stores apply the rules to new reports with these hashes
([store.md](store.md) §4.5).
