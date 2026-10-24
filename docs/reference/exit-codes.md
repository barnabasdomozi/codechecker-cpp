# Reference: exit codes

Usage: [../usage/exit-codes.md](../usage/exit-codes.md)

Summary of the exit codes specified on the individual pages (follow the
links for the exact conditions).

## General rules

- Commands run through the `CodeChecker` launcher: **argument parsing
  errors exit 1** (not argparse's 2), uncaught exceptions print a traceback
  to stdout and exit 1, `Ctrl-C` exits 1 (`Interrupted by user...`),
  `SIGINT`/`SIGTERM` handlers exit `128 + signal`
  ([common-infrastructure.md](common-infrastructure.md) §2, §4).
- The standalone tools (`report-converter`, `tu_collector`,
  `merge-clang-extdef-mappings`, `memory_safety_reporter`,
  `bazel-compile-commands`, `gerrit_changed_files_to_skipfile.py`,
  `post-process-stats`) use plain argparse: **parse errors exit 2**.
- Every client command that connects to a server exits 1 on connection,
  authentication or API errors ([server-api.md](server-api.md) §5) and **2**
  on a malformed product URL ([server-api.md](server-api.md) §3.1).

## Per command

| Command | 0 | 1 | 2 | 3 | Other | Details |
|---|---|---|---|---|---|---|
| `log` | build succeeded | setup error | | | the build's exit code | [log.md](log.md) |
| `analyze` | done, no failed analysis | error | | some analysis failed | | [analyze.md](analyze.md) §6 |
| `check` | no reports printed | error | reports printed | analysis failed, no reports printed | build's exit code if `-b` failed | [check.md](check.md) §3 |
| `parse` | no reports kept, `--status` | error | reports kept | | | [parse.md](parse.md) |
| `fixit` | success (also when `clang-apply-replacements` is missing) | error | | | | [fixit.md](fixit.md) |
| `version`, `analyzer-version`, `web-version` | always | | | | | [version.md](version.md) |
| `analyzers`, `checkers` | success | errors listed there | | | | [analyzers.md](analyzers.md), [checkers.md](checkers.md) |
| `cmd diff` (all modes) | empty result | error | reports or leftover baseline hashes | | | [local-diff.md](local-diff.md) §7 |
| `cmd runs`, `history`, `results`, `sum` | success | error, no matching run | | | | [cmd-runs-history.md](cmd-runs-history.md), [cmd-results.md](cmd-results.md), [cmd-sum.md](cmd-sum.md) |
| `cmd update` | renamed | error, run not found | | | | [cmd-run-management.md](cmd-run-management.md) |
| `cmd del` | done, **also when nothing matched** | error, run locked | | | | same |
| `cmd suppress` | done (unmatched entries ignored) | error, run not found | | | | [cmd-suppress.md](cmd-suppress.md) |
| `cmd export` / `import` | done | no runs / bad file / error | | | | [cmd-export-import.md](cmd-export-import.md) |
| `cmd products list/add/del` | done; `del` of a missing product too | error | | | | [cmd-products.md](cmd-products.md) |
| `cmd components` | done, update declined | error | | | | [cmd-components.md](cmd-components.md) |
| `cmd filter-preset` | done, `list` with no presets | error | | | | [cmd-filter-preset.md](cmd-filter-preset.md) |
| `cmd login`, `token`, `permissions` | done (also "server does not require privileged access") | error | | | | [server-api.md](server-api.md) §7, [server-authentication.md](server-authentication.md) |
| `cmd serverside-tasks -t ONE` (no `--await`) | `COMPLETED` | error | no token and no filter | | 4 `FAILED`, 8 not finished, 16 `CANCELLED`/`DROPPED` | [cmd-serverside-tasks.md](cmd-serverside-tasks.md) §5 |
| `cmd serverside-tasks --await` | all `COMPLETED` (or `CANCELLED` with `--kill`) | any other final status, error | | | | same |
| `store` | stored, or submitted with `--detach` | error, server-side failure (also after a committed store with invalid source comments) | no run name | | | [store.md](store.md) §5 |
| `server` (`-l`, `-s`, `--stop-all`, `-r`) | always (nothing matched too) | error | | | | [server.md](server.md) §3 |
| `server` (running) | | start-up error, port in use | | | `128 + signal` after a signal stop (143 for `SIGTERM`); database paths: config DB upgrade failure exits with the `DBStatus` value | [server.md](server.md) §6, [server-database.md](server-database.md) §3.2 |
| `report-converter` | success, also nothing found | missing input, bad `--meta`, crashes | argparse | | | [report-converter.md](report-converter.md) §1 |
| `tu_collector` | success, also when dependency collection failed | missing database, crashes | argparse | | | [tu-collector.md](tu-collector.md) |
| `merge-clang-extdef-mappings` | success | | argparse | | | [merge-clang-extdef-mappings.md](merge-clang-extdef-mappings.md) |
| `post-process-stats` | success, also for a non-existent input folder (verified) | crashes | argparse | | | [statistical-analysis.md](statistical-analysis.md) §4 |
| `memory_safety_reporter` | success | missing report folder, missing `metadata.json`/`conf`, subcommand failure | argparse | | | [memory-safety-reporter.md](memory-safety-reporter.md) |
| `bazel-compile-commands` | success | no command, no targets, no output, missing `bazel` | argparse | | | [bazel-compile-commands.md](bazel-compile-commands.md) |
| `gerrit_changed_files_to_skipfile.py` | success | invalid JSON line | argparse, unreadable/unwritable file | | | [gerrit-integration.md](gerrit-integration.md) |
