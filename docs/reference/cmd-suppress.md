# Reference: `CodeChecker cmd suppress`

Usage: [../usage/cmd-suppress.md](../usage/cmd-suppress.md)

Python source: `web/client/codechecker_client/cli/cmd.py`
(`__register_suppress`), `web/client/codechecker_client/cmd_line_client.py`
(`handle_suppress`, `check_run_names`),
`web/client/codechecker_client/suppress_file_handler.py`
(`get_suppress_data`), `web/server/codechecker_server/api/report_server.py`
(`getRunResults`, `changeReviewStatus`).

## 1. Command line

```
CodeChecker cmd suppress RUN_NAME -i/--import SUPPRESS_FILE [-f/--force]
                         [--url PRODUCT_URL] [--verbose L]
```

`-i` is required. `-f` is accepted but **not used** by the handler (§3).

## 2. Flow

1. Results client ([server-api.md](server-api.md) §6).
2. Look up `RUN_NAME` exactly (`exactMatch = true`); missing → WARNING `The
   following runs were not found in the database: <name>.`, exit 1.
3. Parse the suppress file (§2.1).
4. For each entry `(hash, file name, comment, status)`:
   1. `getRunResults([runId], 500, 0, null,
      ReportFilter(filepath=["%" + file name], reportHash=[hash]), null,
      false)` — a single page (at most 500 reports). The file filter is an
      ILIKE pattern, so it matches any stored path **ending** with the file
      name (`%` and `_` in names act as wildcards, `*` too);
   2. for each returned report: `changeReviewStatus(reportId, S, comment)`
      with `S` = `CONFIRMED` for `confirmed`, `INTENTIONAL` for
      `intentional`, otherwise `FALSE_POSITIVE` (also for
      `false_positive`, `suppress` and entries without a status).
5. Exit 0. Nothing is printed except the login lines; entries that match no
   report are silently ignored.

Default results filters do **not** apply here (the filter only has the two
fields), so reports of every detection and review status match.

### 2.1 Suppress file lines

Each line (stripped) is matched against, in this order (`H` = 32 word
characters, optional `#<digit>` hash type ignored):

1. `H||<file name>||<comment>||<status>` — file name: one or more
   characters other than `\` and `|`; comment: no `|`; status: zero or more
   characters from the set of letters used in the source-code comment
   markers (`a c d e f h i k l m n o p r s t u v _ |`), in practice
   `false_positive`, `intentional`, `confirmed` or `suppress`;
2. `H||<file name>||<comment>` — status `false_positive`;
3. `H||<comment>` — old format, file name `""` (the filter becomes `%`,
   i.e. any file).

Whitespace is allowed before each `||` and after it, but the file name and
the comment keep their **trailing** spaces (`H || main.c || c2 || intentional`
gives file name `"main.c "` and comment `"c2 "`; verified), and a file name
with a trailing space then matches no stored path. Any other non-empty
line → WARNING `Malformed suppress line: <line>` (e.g. status `bogus`) and
is skipped. Same format as in
[review-status-and-suppression.md](review-status-and-suppression.md) §5.

## 3. Server `changeReviewStatus(reportId, status, message)`

Requires `PRODUCT_ACCESS` or `PRODUCT_STORE`.

1. Unknown id → `DATABASE "No report found in the database."`
2. Intended: when the new status is `false_positive`/`intentional` and the
   report is open (detection `new`/`unresolved`/`reopened`, review status
   not FP/intentional) set `fixed_at = now`; when moving an open FP/
   intentional report back, clear `fixed_at`. ⚠️ The code compares the
   **integer** enum value with the strings, so the first branch never runs:
   marking a report false positive or intentional this way leaves
   `fixed_at` NULL (verified), and the report still counts as outstanding
   in `--report-status` filters and statistics. The second branch runs for
   every new status when the report was FP/intentional and open.
3. Update that **one report**: `review_status`, `review_status_author` =
   caller (`Anonymous` without authentication), `review_status_message` =
   message, `review_status_date = now`. `review_status_is_in_source` is not
   changed and in-source statuses are overwritten too.
4. INFO `Review status of report '<id>' was changed to '<status>' by <user>.`

No review status rule (`review_statuses` row) and no system comment are
created, and the product's "review status change disabled" flag is not
checked. Reports with the same hash in other runs, and future stores, are
not affected.

⚠️ Because `-f` is ignored, existing review statuses are always overwritten
(verified: a second import with `confirmed` replaced `intentional` without
`-f`). The help text and the usage page say they are kept without `-f`.
