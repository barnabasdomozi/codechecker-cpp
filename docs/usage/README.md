# CodeChecker usage reference

These pages describe everything a user can do with CodeChecker **6.30.0**
(the Python version, [Ericsson/codechecker](https://github.com/Ericsson/codechecker/tree/451b0c5c1786ed21b0685c298f759164a8c5d379) at commit `451b0c5c`).
Source paths mentioned in these pages (e.g. `web/server/...`) are relative to
that repository. Each page covers one use case. The
C++ port must support the same use cases with the same command line, output
formats and exit codes.

Every command shown here was run against a real CodeChecker 6.30.0 build on
2026-10-23. On 2026-10-24 Cppcheck and PostgreSQL were installed on the test
machine; the outputs were re-checked and updated with the black-box test
suite ([testing/](../../testing/README.md)), which runs
the use cases of these pages. Each page marks its use cases like this:

| Mark | Meaning |
|------|---------|
| ✅ | Ran and behaved as described. |
| ⚠️ | Ran, but the behavior differs from the help text or has a bug. The difference is described. |
| ❌ | Could not be run on the test machine (a needed tool was missing). Described from the help text and source code. |

Bugs and help-text mismatches found while testing are collected in
[known-issues.md](known-issues.md). The port should decide for each one
whether to copy the behavior or fix it.

## Test environment

- CodeChecker 6.30.0 (git `451b0c5c`), Thrift API 6.74, Python 3.13.
- Analyzers found: Clang Static Analyzer 19.1.7, Clang-Tidy 19.1.7,
  GCC 14.2.0, Cppcheck 2.17.1. **Not installed:** Facebook Infer.
- PostgreSQL 17 (for the PostgreSQL use cases a temporary server was used).
- Not installed: Bazel.
- A plain `CodeChecker analyze` runs every analyzer it finds, so the outputs
  on these pages include Cppcheck's reports. Without Cppcheck, the
  `cppcheck-*` reports are missing, and Cppcheck is listed as `NOT FOUND`
  and under "Missing analysis results".
- CodeChecker was built from source (`make package` in the CodeChecker
  repository). The CodeChecker on `PATH` needs the Python packages of the
  repository's development virtual environment (`make venv_dev`), so activate
  it first: `source venv_dev/bin/activate` (run in the CodeChecker repository).

## Demo project

All examples use the small project in [demo-project.md](demo-project.md). It
has known bugs that the analyzers find, plus source-code suppression comments.

## Pages

### Information commands
- [version.md](version.md) – `CodeChecker version`, `analyzer-version`, `web-version`
- [analyzers.md](analyzers.md) – list analyzers and their configuration options
- [checkers.md](checkers.md) – list checkers, labels, profiles, guidelines, severities

### Analysis
- [log.md](log.md) – record build commands into a compilation database (`CodeChecker log`, `ldlogger`)
- [analyze.md](analyze.md) – run the analyzers (`CodeChecker analyze`): inputs, outputs, jobs, exit codes
- [checker-selection.md](checker-selection.md) – enable and disable checkers, groups, profiles, labels
- [analyzer-configuration.md](analyzer-configuration.md) – analyzer and checker options, verbatim arguments, `.clang-tidy`
- [compiler-info.md](compiler-info.md) – implicit include paths, target and standard detection
- [config-file.md](config-file.md) – put command-line options in a YAML/JSON file (`--config`)
- [skip-and-file-filters.md](skip-and-file-filters.md) – skip files and analyze only some files
- [ctu-analysis.md](ctu-analysis.md) – cross translation unit analysis
- [statistical-analysis.md](statistical-analysis.md) – statistics-based checkers, `post-process-stats`
- [makefile-mode.md](makefile-mode.md) – generate a Makefile instead of analyzing
- [analysis-debugging.md](analysis-debugging.md) – capture analyzer output, reproducers, failed analyses, timeouts
- [check.md](check.md) – log + analyze + print in one step (`CodeChecker check`)

### Working with results locally
- [parse.md](parse.md) – print results in the terminal (`CodeChecker parse`)
- [parse-export.md](parse-export.md) – export to HTML, JSON, Code Climate, SARIF, Gerrit, baseline
- [review-status-and-suppression.md](review-status-and-suppression.md) – source-code comments, `review_status.yaml`, suppress files
- [fixit.md](fixit.md) – list and apply automatic fixes (`CodeChecker fixit`)
- [local-diff.md](local-diff.md) – compare report folders and baseline files without a server

### Server
- [server.md](server.md) – start, list, reload and stop servers; database options; HTTPS
- [server-database.md](server-database.md) – database status and schema upgrade
- [server-authentication.md](server-authentication.md) – login, logout, password file, personal access tokens, permissions
- [store.md](store.md) – upload results to a server (`CodeChecker store`)
- [web-ui.md](web-ui.md) – view results in a browser

### Command-line client (`CodeChecker cmd`)
- [cmd-runs-history.md](cmd-runs-history.md) – list runs and run history
- [cmd-results.md](cmd-results.md) – list reports with filters
- [cmd-sum.md](cmd-sum.md) – checker statistics
- [cmd-diff.md](cmd-diff.md) – compare runs on a server
- [cmd-run-management.md](cmd-run-management.md) – rename and delete runs
- [cmd-suppress.md](cmd-suppress.md) – import a suppress file into a server
- [cmd-export-import.md](cmd-export-import.md) – export and import comments and review statuses
- [cmd-products.md](cmd-products.md) – manage products
- [cmd-components.md](cmd-components.md) – manage source components
- [cmd-filter-preset.md](cmd-filter-preset.md) – saved result filters
- [cmd-serverside-tasks.md](cmd-serverside-tasks.md) – query, await and cancel background tasks

### Other tools shipped with CodeChecker
- [report-converter.md](report-converter.md) – convert other analyzers' output into CodeChecker reports
- [tu-collector.md](tu-collector.md) – collect the source files of translation units
- [merge-clang-extdef-mappings.md](merge-clang-extdef-mappings.md) – merge CTU definition maps
- [gerrit-integration.md](gerrit-integration.md) – Gerrit review output and changed-files skipfile
- [memory-safety-reporter.md](memory-safety-reporter.md) – package results for memory safety reporting
- [bazel-compile-commands.md](bazel-compile-commands.md) – compilation database from Bazel

### Reference
- [environment-variables.md](environment-variables.md) – every `CC_*` variable
- [exit-codes.md](exit-codes.md) – exit codes of every command
- [known-issues.md](known-issues.md) – bugs and help-text mismatches found while testing

## Conventions used in the examples

- The demo project is in `~/demo`. Paths in the sample output are shortened
  to match.
- `localhost:8555` is the test server; the default port is `8001`.
- Long tables in the sample output are cut short with `...`.
- Every command accepts `-h/--help` and `--verbose {info,debug_analyzer,debug}`.
  These are not repeated on every page.
