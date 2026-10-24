# Known issues in CodeChecker 6.30.0

Bugs and help-text mismatches found while testing the use cases. For each one,
the port must decide whether to copy the behavior (compatibility) or fix it.
The **Suggestion** column is a recommendation only.

## Bugs

| # | Area | What happens | Suggestion | Page |
|---|---|---|---|---|
| 1 | `server --db-status`, `--db-upgrade-schema` | Options are ignored and a normal server starts on port 8001. Cause: `context.migration_root` does not exist (renamed to `run_migration_root`); the `AttributeError` is swallowed by `except AttributeError`. | Fix: print the status / upgrade, then exit. | [server-database.md](server-database.md) |
| 2 | `fixit` reading `cmd diff -o json` from stdin | `cmd diff -o json` writes the local report format (no `checkedFile`), so `fixit` crashes with `KeyError: 'checkedFile'` whenever the diff has reports; an empty diff disables filtering entirely. Only `cmd results -o json` works. | Fix: accept both JSON report formats; treat an empty list as "nothing to fix". | [fixit.md](fixit.md) |
| 3 | `fixit` without `clang-apply-replacements` | Logs an error but exits `0`. | Fix: exit non-zero. | [fixit.md](fixit.md) |
| 4 | `cmd diff -o table` (local folders) | Column headers do not match the data (File/Checker/Severity/Msg are shifted). | Fix. | [local-diff.md](local-diff.md) |
| 5 | `cmd diff -b FOLDER -n X.baseline --new` | Finds 0 reports but exits `2`. | Fix: exit `0` when the difference is empty. | [local-diff.md](local-diff.md) |
| 6 | `store --zip-loc DIR` | Crashes with `FileNotFoundError` if `DIR` does not exist. | Fix: create it or give a clear error. | [store.md](store.md) |
| 7 | `cmd components add` for an existing name | Asks `Do you want to update? Y(es)/n(o)`; crashes with `EOFError` when stdin is closed. | Fix: add a `--force`/non-interactive path. | [cmd-components.md](cmd-components.md) |
| 8 | `cmd sum` | The "All reports" column excludes false positives that the "False positive" column counts; the long checker table contains `__FAKE__` and `NOT FOUND` rows. | Decide; at least drop the placeholder rows. | [cmd-sum.md](cmd-sum.md) |
| 9 | `--single-origin-report` | With `--component`, returned nothing even for reports whose whole path is inside the component. Cause: the "exclude" sub-queries join on `file.id != report.file_id` while filtering files to the component, so any component with a second file excludes every report. | Fix: exclude reports with a path point in a file **outside** the component. | [cmd-results.md](cmd-results.md) |
| 10 | `tu_collector -b "make"` | Prints `Couldn't create dependencies:` and writes an almost empty zip, exit `0`. | Fix: exit non-zero. | [tu-collector.md](tu-collector.md) |
| 11 | Unauthenticated access to an auth server | Message blames the password file ("Your password has been changed...") even when no password file exists. | Fix the message. | [server-authentication.md](server-authentication.md) |
| 12 | `server` startup | Warns "Number of worker processes can not be negative!" for the default `null` setting. | Fix. | [server.md](server.md) |
| 13 | Makefile mode | clang-tidy result file names differ from `analyze`, so `parse --status` reports clang-tidy results as missing. | Fix: use the same names. | [makefile-mode.md](makefile-mode.md) |
| 14 | Source-file / folder input to `analyze` | When no `compile_commands.json` is found, it warns "No analysis is required" and exits `0`. | Consider exit `1`. | [analyze.md](analyze.md) |
| 15 | `cmd diff` with two local sides | `--report-hash`, `--analyzer-name`, `--bug-path-length`, `--filter-preset`, `--detection-status`, `--report-status`, `--tag`, `--uniqueing` and the date filters are accepted but ignored. | Fix: apply them or reject them. | [local-diff.md](local-diff.md) |
| 16 | `cmd diff` / `cmd results` filter values | An unknown `--severity`, `--review-status` or `--detection-status` value crashes with `KeyError` (e.g. `--review-status suppress`). The intended "Invalid ... value(s)" message is unreachable. | Fix: print the error and exit `1`. | [cmd-results.md](cmd-results.md) |
| 17 | `cmd diff` crashes | `-c` without `-e` (`TypeError`); `--component` when both sides are local (`AttributeError`). | Fix. | [local-diff.md](local-diff.md) |
| 18 | `cmd diff` between a server run and a local folder | Local reports with review status `confirmed` are treated as suppressed and left out of `--new`/`--unresolved` (verified: a `confirmed` rule in `review_status.yaml` removed the report from `--unresolved`). Cause: a status object is compared with strings. | Fix: only exclude statuses outside `--review-status`. | [cmd-diff.md](cmd-diff.md) |
| 19 | `review_status.yaml` | A list as a `report_hash`/`filepath` filter value (`report_hash: [abc]`) crashes `cmd diff`/`parse` with `TypeError`; a list for `checker_name` silently never matches. | Fix: validate the value type. | [review-status-and-suppression.md](review-status-and-suppression.md) |
| 20 | `cmd history -n PATTERN` | A pattern that matches no run prints the history of all runs; only the newest 500 entries are fetched. | Fix: print nothing (or warn) for no match; page the results. | [cmd-runs-history.md](cmd-runs-history.md) |

## Help text that does not match the behavior

| # | Command | Help says | Actual | Page |
|---|---|---|---|---|
| H1 | `analyze -e/-d` | Compiler warnings are enabled as `-e Wliteral-conversion` | `W...` names are rejected; use `clang-diagnostic-<name>` | [checker-selection.md](checker-selection.md) |
| H2 | `analyzers --dump-config` | Only clang-tidy supports it | `clangsa` prints Clang's analyzer option help | [analyzers.md](analyzers.md) |
| H3 | `cmd sum -n` | Several runs separated by `:` | `:` finds no runs; use spaces | [cmd-sum.md](cmd-sum.md) |
| H4 | `server --db-upgrade-schema` + `--db-force-upgrade` | Can be combined | Mutually exclusive | [server-database.md](server-database.md) |
| H5 | `tu_collector -f` with `-z` | Absolute path patterns (`/path/to/*/files`) | Matched against the raw `file` field, often relative | [tu-collector.md](tu-collector.md) |
| H6 | `analyze --file` | Can contain path globs; headers select the sources including them | Header selection works only for an existing **absolute** header path; a relative header path finds no dependents and a header glob is treated as a plain pattern | [skip-and-file-filters.md](skip-and-file-filters.md) |
| H7 | `cmd diff --report-status`, `--review-status`, `--detection-status`, `--tag`, `--component` | Only usable with a server run | `--review-status` works for local folders too; the others are accepted silently (or crash, `--component`) | [local-diff.md](local-diff.md) |
| H8 | `cmd diff -o html` without `-e` | – | Error message names `parse` options `--export`/`--output` | [local-diff.md](local-diff.md) |
| H9 | `checkers -w` | Shows warning flags (deprecated) | Prints the normal checker list | [checkers.md](checkers.md) |
| H10 | `analyzers --all`, `--details` | Deprecated | No effect | [analyzers.md](analyzers.md) |
| H11 | `analyze --ctu-ast-mode` | Default is `parse-on-demand` | Without the option, ASTs are dumped (`load-from-pch` behavior) | [ctu-analysis.md](ctu-analysis.md) |
| H12 | `analyze --disable-all` | Disables all checkers of all analyzers | Inserts `-d default` (the `default` profile) before the other `-e/-d` options; checkers outside the default profile were already disabled, so the effect is the same unless `--enable-all` is also given | [checker-selection.md](checker-selection.md) |
| H13 | `store` without `-f` | Reports from files not affected by the analysis are kept | Every old report not in the upload becomes `resolved`/`off`/`unavailable` | [store.md](store.md) |

## Environment notes (not CodeChecker bugs)

- Statistical analysis needs Clang's `statisticsCollector` checkers, which
  upstream Clang 19 lacks.
- On Debian, `clang-apply-replacements` and `clang-extdef-mapping` are only in
  `/usr/lib/llvm-19/bin` (or with a `-19` suffix), not on `PATH`.
- The installed `bin/CodeChecker` needs the Python packages of the CodeChecker
  repository's development virtual environment (`venv_dev`, e.g. `argcomplete`,
  `lxml`).

## Found while writing the reference pages

Found in the source code while writing [../reference/](../reference/README.md).
"✔" = verified against the 6.30.0 build; the others are from the source
code only. The **Page** column links to the reference page that describes
the behavior.

| # | Area | What happens | Suggestion | Page |
|---|---|---|---|---|
| 21 | `server -r` (`SIGHUP`) | The reload only reaches the main process. The API worker processes keep the old configuration, so the reload has no effect ✔ (e.g. a changed `max_run_count` is not applied). | Fix: apply the reload where requests are served. | [server.md](../reference/server.md) |
| 22 | `server` start-up check | "There are some database issues" only looks at the first product (the loop stops after one). | Fix: check every product. | [server-database.md](../reference/server-database.md) |
| 23 | `--db-upgrade-schema P` (intended code path, see #1) | Checks that `P` exists, then upgrades **all** products. | Fix: upgrade only `P`. | [server-database.md](../reference/server-database.md) |
| 24 | `cmd token new` | Without `--expiration` it always fails with `Invalid expiration length. The length must be between 1 and 365!` ✔ | Fix: default to the server maximum. | [server-authentication.md](../reference/server-authentication.md) |
| 25 | `store` with invalid source-code comments (e.g. two comments for one report) | The run is committed, then the task fails with `Row has incorrect number of values, (actual) 1!=3 (expected)` (the code that builds the error table crashes); `store` exits 1 ✔ | Fix: report the comment errors clearly; decide whether to store or reject before committing. | [store.md](../reference/store.md) |
| 26 | `store` with several report folders | `off`/`unavailable` **are** computed (the help text says they are not). A checker disabled in one folder and enabled in another becomes `off` ✔ | Fix the help text; treat "enabled anywhere" as enabled. | [store.md](../reference/store.md) |
| 27 | `store` report annotations | An unknown key or a badly formatted value fails the task with `type object 'ErrorCode' has no attribute 'REPORT_FORMAT'` instead of the intended message. | Fix: report the intended message. | [store.md](../reference/store.md) |
| 28 | `store` and review status rules | The rule's author and date are not copied to newly stored reports (misspelled attribute names); they keep the storing user and the store time. | Fix: copy them. | [store.md](../reference/store.md) |
| 29 | `cmd suppress -f` | `-f` is ignored: existing review statuses are always overwritten ✔ (the usage page and help say they are kept without `-f`). | Fix: honour `-f`. | [cmd-suppress.md](../reference/cmd-suppress.md) |
| 30 | `cmd suppress` (`changeReviewStatus`) | Setting false positive / intentional never sets `fixed_at` (an integer is compared with strings), so the report still counts as outstanding ✔ | Fix the comparison. | [cmd-suppress.md](../reference/cmd-suppress.md) |
| 31 | `cmd del -n` | Names are case-insensitive patterns with `*` wildcards ✔ (the usage page says "no wildcards"). | Document it (or match exactly). | [cmd-run-management.md](../reference/cmd-run-management.md) |
| 32 | `cmd export` → `cmd import` | Import fails for whole-second timestamps: export writes `2026-01-02 03:04:05`, import requires `.%f` ✔ | Fix: accept both forms. | [cmd-export-import.md](../reference/cmd-export-import.md) |
| 33 | `cmd import` | Imported review status rules get the importing user as author, not the exported author. | Decide; probably keep the exported author. | [cmd-export-import.md](../reference/cmd-export-import.md) |
| 34 | `cmd products add --sqlite Default.sqlite` | Accepted: two products then share one database ✔ (the "already in use" check misses relative SQLite paths). | Fix the check. | [cmd-products.md](../reference/cmd-products.md) |
| 35 | `cmd products del MISSING` | Prints `The product '...' does not exist!` but exits `0` ✔ | Fix: exit `1`. | [cmd-products.md](../reference/cmd-products.md) |
| 36 | `cmd serverside-tasks` filters | `--no-username` and `--no-product` are ignored; `--last-seen-before/after` send the `--started-before/after` values ✔ | Fix. | [cmd-serverside-tasks.md](../reference/cmd-serverside-tasks.md) |
| 37 | `cmd serverside-tasks` permissions | Viewing/listing product tasks needs `PRODUCT_ACCESS` (the API docs say `PRODUCT_ADMIN`); the task's submitter can cancel it (the usage page says superusers only). | Decide and document. | [cmd-serverside-tasks.md](../reference/cmd-serverside-tasks.md) |
| 38 | `cmd components add` (update) | The stored user name is not updated (wrong attribute name). | Fix. | [cmd-components.md](../reference/cmd-components.md) |
| 39 | Password file (`~/.codechecker.passwords.json`) | Invalid JSON or a missing `credentials` key crashes with `KeyError: 'credentials'` ✔; `cmd login` cuts a saved password at its second `:`. | Fix: clear error; split at the first `:` only. | [server-api.md](../reference/server-api.md) |
| 40 | Server URL (`--url` of `login`, `token`, `permissions`, `products`, `serverside-tasks`) | An unknown protocol or a non-numeric port prints a `ValueError` traceback ✔ | Fix: error message, exit `2` like product URLs. | [server-api.md](../reference/server-api.md) |
| 41 | `report-converter --meta analyzer_command=...` | The command is stored on the server with a space between every character (`a b` → `a   b`) ✔ | Fix: write `command` as a list in `metadata.json`. | [report-converter.md](../reference/report-converter.md) |
| 42 | `report-converter` arguments | `--meta` swallows the inputs that follow it; a `--meta` value without `=` and an empty input directory crash with tracebacks ✔ | Fix: clear errors. | [report-converter.md](../reference/report-converter.md) |
| 43 | `report-converter -t clang-tidy` | A blank line after a diagnostic aborts the whole conversion (`Expected a FixIt line!`, exit `1`) ✔ | Fix: treat it as the end of the diagnostic. | [report-converter.md](../reference/report-converter.md) |
| 44 | `tu_collector -l` | Compilation databases that use `arguments` instead of `command` crash with `KeyError: 'command'` ✔ | Fix: support `arguments`. | [tu-collector.md](../reference/tu-collector.md) |
| 45 | `gerrit_changed_files_to_skipfile.py` | Pretty-printed (multi-line) JSON gives a skipfile with only `-*`, which skips every file, without a warning ✔ | Fix: parse the whole file. | [gerrit-integration.md](../reference/gerrit-integration.md) |
| 46 | `memory_safety_reporter` | A report folder without `metadata.json` or `conf/` crashes with `FileNotFoundError` ✔ | Fix: clear error, or skip the missing part. | [memory-safety-reporter.md](../reference/memory-safety-reporter.md) |
| 47 | `post-process-stats` | A non-existent input folder prints `Done.` and exits `0` ✔ | Fix: error, exit `1`. | [exit-codes.md](../reference/exit-codes.md) |
