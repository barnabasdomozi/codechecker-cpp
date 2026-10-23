# List runs and run history (`cmd runs`, `cmd history`)

Every `cmd` subcommand takes `--url` (default `localhost:8001/Default`; for
server-wide commands, `localhost:8001`) and most take
`-o {plaintext,rows,table,csv,json}` (default `plaintext`, which is a table).
Set `COLUMNS` to a wide value to keep tables from wrapping.

## List runs ✅

```sh
CodeChecker cmd runs --url localhost:8555/Default
```

```
┌──────────┬──────────────────────────────┬─────────────────────────────────┬────────────────────────────┬─────────────┬──────────┬─────────────┬─────────────────────┐
│ Name     │ Number of unresolved reports │ Analyzer statistics             │ Storage date               │ Version tag │ Duration │ Description │ CodeChecker version │
├──────────┼──────────────────────────────┼─────────────────────────────────┼────────────────────────────┼─────────────┼──────────┼─────────────┼─────────────────────┤
│ cfg_run  │ 3                            │ clangsa (3/3), clang-tidy (3/3) │ 2026-10-23 23:52:41.783584 │ from-config │ 0:00:00  │             │ 6.30 (451b0c5c...)  │
│ multi    │ 4                            │ clang-tidy (3/3), clangsa (3/3) │ 2026-10-23 23:39:56.492370 │             │ 0:00:00  │             │ 6.30 (451b0c5c...)  │
│ demo     │ 4                            │ clangsa (3/3), clang-tidy (3/3) │ 2026-10-23 23:39:49.951933 │ v2          │ 0:00:00  │             │ 6.30 (451b0c5c...)  │
└──────────┴──────────────────────────────┴─────────────────────────────────┴────────────────────────────┴─────────────┴──────────┴─────────────┴─────────────────────┘
```

`-o plaintext` and `-o table` print the same table. `-o rows` prints one
line per run without borders:

```
 demo 4 clangsa (3/3), clang-tidy (3/3) 2026-10-23 23:39:49.951933 v2 0:00:00  6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379)
```

"Analyzer statistics" shows successful/total analyzed files per analyzer.

## Filter and sort runs ✅

```sh
CodeChecker cmd runs -n "demo*" --url localhost:8555/Default            # names with * wildcards; several allowed
CodeChecker cmd runs --sort name --order asc -o table --url localhost:8555/Default
CodeChecker cmd runs --all-after-run demo --url localhost:8555/Default   # stored after run "demo"
CodeChecker cmd runs --all-before-run demo --url localhost:8555/Default
CodeChecker cmd runs --all-after-time 2026:10:23 --url localhost:8555/Default
CodeChecker cmd runs --all-before-time 2030:01:01 --url localhost:8555/Default
```

- `--sort {name,unresolved_reports,date,duration,codechecker_version}`
  (default `date`), `--order {asc,desc}` (default `desc`).
- Timestamps are `year:month:day[:hour:minute:second]`; without the time,
  midnight is used.
- `-n`, `--all-*-run` and `--all-*-time` exclude each other.

## CSV and JSON ✅

```sh
CodeChecker cmd runs -o csv --url localhost:8555/Default
CodeChecker cmd runs --details -o json -n demo --url localhost:8555/Default
```

```
Name,Number of unresolved reports,Analyzer statistics,Storage date,Version tag,Duration,Description,CodeChecker version
demo,4,clangsa (3/3), clang-tidy (3/3),2026-10-23 23:39:49.951933,v2,0:00:00,,6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379)
```

```json
[
    {
        "demo": {
            "runId": 1,
            "runDate": "2026-10-23 23:39:49.951933",
            ...
        }
    }
]
```

`--details` adds more data to the JSON output, e.g. the enabled checkers.

## Run history ✅

Every `store` into a run adds a history entry.

```sh
CodeChecker cmd history -n demo --url localhost:8555/Default
CodeChecker cmd history --url localhost:8555/Default        # all runs
```

```
┌────────────────────────────┬──────────┬─────────────┬───────────┬─────────────────────┬─────────────────────────────────┬─────────────┐
│ Date                       │ Run name │ Version tag │ User      │ CodeChecker version │ Analyzer statistics             │ Description │
├────────────────────────────┼──────────┼─────────────┼───────────┼─────────────────────┼─────────────────────────────────┼─────────────┤
│ 2026-10-23 23:39:49.942365 │ demo     │ v2          │ Anonymous │ 6.30 (451b0c5c...)  │ clangsa (3/3), clang-tidy (3/3) │             │
│ 2026-10-23 23:39:43.597604 │ demo     │ v1          │ Anonymous │ 6.30 (451b0c5c...)  │ clangsa (3/3), clang-tidy (3/3) │ first run   │
└────────────────────────────┴──────────┴─────────────┴───────────┴─────────────────────┴─────────────────────────────────┴─────────────┘
```

"User" is `Anonymous` on servers without authentication.

Exit code: `0` (`1` on connection or authentication errors).
