# Known issues in CodeChecker 6.30.0

Bugs and help-text mismatches found while testing the use cases. For each one,
the port must decide whether to copy the behavior (compatibility) or fix it.
The **Suggestion** column is a recommendation only.

## Bugs

| # | Area | What happens | Suggestion | Page |
|---|---|---|---|---|
| 1 | `server --db-status`, `--db-upgrade-schema` | Options are ignored and a normal server starts on port 8001. Cause: `context.migration_root` does not exist (renamed to `run_migration_root`); the `AttributeError` is swallowed by `except AttributeError`. | Fix: print the status / upgrade, then exit. | [server-database.md](server-database.md) |
| 2 | `fixit` reading `cmd diff -o json` from stdin | Crashes with `KeyError: 'checkedFile'` when the diff is local-vs-local or server-vs-server. Works only for server-run vs. local folder. | Fix: accept both JSON report formats. | [fixit.md](fixit.md) |
| 3 | `fixit` without `clang-apply-replacements` | Logs an error but exits `0`. | Fix: exit non-zero. | [fixit.md](fixit.md) |
| 4 | `cmd diff -o table` (local folders) | Column headers do not match the data (File/Checker/Severity/Msg are shifted). | Fix. | [local-diff.md](local-diff.md) |
| 5 | `cmd diff -b FOLDER -n X.baseline --new` | Finds 0 reports but exits `2`. | Fix: exit `0` when the difference is empty. | [local-diff.md](local-diff.md) |
| 6 | `store --zip-loc DIR` | Crashes with `FileNotFoundError` if `DIR` does not exist. | Fix: create it or give a clear error. | [store.md](store.md) |
| 7 | `cmd components add` for an existing name | Asks `Do you want to update? Y(es)/n(o)`; crashes with `EOFError` when stdin is closed. | Fix: add a `--force`/non-interactive path. | [cmd-components.md](cmd-components.md) |
| 8 | `cmd sum` | The "All reports" column excludes false positives that the "False positive" column counts; the long checker table contains `__FAKE__` and `NOT FOUND` rows. | Decide; at least drop the placeholder rows. | [cmd-sum.md](cmd-sum.md) |
| 9 | `--single-origin-report` | With `--component`, returned nothing even for reports whose whole path is inside the component. | Investigate. | [cmd-results.md](cmd-results.md) |
| 10 | `tu_collector -b "make"` | Prints `Couldn't create dependencies:` and writes an almost empty zip, exit `0`. | Fix: exit non-zero. | [tu-collector.md](tu-collector.md) |
| 11 | Unauthenticated access to an auth server | Message blames the password file ("Your password has been changed...") even when no password file exists. | Fix the message. | [server-authentication.md](server-authentication.md) |
| 12 | `server` startup | Warns "Number of worker processes can not be negative!" for the default `null` setting. | Fix. | [server.md](server.md) |
| 13 | Makefile mode | clang-tidy result file names differ from `analyze`, so `parse --status` reports clang-tidy results as missing. | Fix: use the same names. | [makefile-mode.md](makefile-mode.md) |
| 14 | Source-file / folder input to `analyze` | When no `compile_commands.json` is found, it warns "No analysis is required" and exits `0`. | Consider exit `1`. | [analyze.md](analyze.md) |

## Help text that does not match the behavior

| # | Command | Help says | Actual | Page |
|---|---|---|---|---|
| H1 | `analyze -e/-d` | Compiler warnings are enabled as `-e Wliteral-conversion` | `W...` names are rejected; use `clang-diagnostic-<name>` | [checker-selection.md](checker-selection.md) |
| H2 | `analyzers --dump-config` | Only clang-tidy supports it | `clangsa` prints Clang's analyzer option help | [analyzers.md](analyzers.md) |
| H3 | `cmd sum -n` | Several runs separated by `:` | `:` finds no runs; use spaces | [cmd-sum.md](cmd-sum.md) |
| H4 | `server --db-upgrade-schema` + `--db-force-upgrade` | Can be combined | Mutually exclusive | [server-database.md](server-database.md) |
| H5 | `tu_collector -f` with `-z` | Absolute path patterns (`/path/to/*/files`) | Matched against the raw `file` field, often relative | [tu-collector.md](tu-collector.md) |
| H6 | `analyze --file` | Can contain path globs | Headers never match; only `file` entries of the compilation database | [skip-and-file-filters.md](skip-and-file-filters.md) |
| H7 | `cmd diff --report-status` | Only usable with a server run | Accepted silently for local folders | [local-diff.md](local-diff.md) |
| H8 | `cmd diff -o html` without `-e` | – | Error message names `parse` options `--export`/`--output` | [local-diff.md](local-diff.md) |
| H9 | `checkers -w` | Shows warning flags (deprecated) | Prints the normal checker list | [checkers.md](checkers.md) |
| H10 | `analyzers --all`, `--details` | Deprecated | No effect | [analyzers.md](analyzers.md) |

## Environment notes (not CodeChecker bugs)

- Statistical analysis needs Clang's `statisticsCollector` checkers, which
  upstream Clang 19 lacks.
- On Debian, `clang-apply-replacements` and `clang-extdef-mapping` are only in
  `/usr/lib/llvm-19/bin` (or with a `-19` suffix), not on `PATH`.
- The installed `bin/CodeChecker` needs the Python packages from
  `codechecker/venv_dev` (e.g. `argcomplete`, `lxml`).
