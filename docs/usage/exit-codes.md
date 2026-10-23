# Exit codes

| Command | 0 | 1 | 2 | 3 | Other |
|---|---|---|---|---|---|
| `log` | build succeeded | | | | the build command's exit code |
| `analyze` | finished | CodeChecker error | | a translation unit failed | 128+signal |
| `check` | no reports | error | reports found | a translation unit failed | |
| `parse` | no reports (or `--status`) | error | reports found | | |
| `fixit` | success (⚠️ also when `clang-apply-replacements` is missing) | error | | | |
| `cmd diff` | no difference | error | difference found | | |
| `cmd serverside-tasks -t ONE` | completed | operational error | command-line error | | 4 failed, 8 running, 16 cancelled |
| `cmd serverside-tasks --await` | task(s) completed (or killed with `--kill`) | | | | non-zero otherwise |
| other `cmd` subcommands | success | error | | | |
| `store` | success | error | | | |
| `server` | clean stop | startup error | | | 128+signal when killed |
| `version`, `analyzers`, `checkers` | always | | | | |
| `report-converter`, `tu_collector`, `post-process-stats`, `merge-clang-extdef-mappings`, `memory_safety_reporter` | success (even when nothing was found) | error | | | |

Seen during testing: every `0`, `1`, `2` and `3` above, and `143` for a
server stopped with `SIGTERM`. Taken from the help text only:
`serverside-tasks` codes 4, 8 and 16, and the `analyze` 128+signal case.

Command-line parsing errors (bad option values, conflicting options) exit with
`1`, not argparse's usual `2`. Examples:
`analyze --checker-config clang-tidy:a.b=1` and
`server --db-upgrade-schema all --db-force-upgrade`.
