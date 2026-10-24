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
┌─────────┬──────────────────────────────┬─────────────────────────────────────────────────┬────────────────────────────┬─────────────┬──────────┬─────────────┬─────────────────────────────────────────────────┐
│ Name    │ Number of unresolved reports │ Analyzer statistics                             │ Storage date               │ Version tag │ Duration │ Description │ CodeChecker version                             │
├─────────┼──────────────────────────────┼─────────────────────────────────────────────────┼────────────────────────────┼─────────────┼──────────┼─────────────┼─────────────────────────────────────────────────┤
│ cfg_run │ 6                            │ clangsa (3/3), clang-tidy (3/3), cppcheck (3/3) │ 2026-10-24 18:01:41.658051 │ from-config │ 0:00:00  │             │ 6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379) │
│ multi   │ 15                           │ clangsa (6/6), clang-tidy (6/6), cppcheck (6/6) │ 2026-10-24 18:01:26.423033 │             │ 0:00:00  │             │ 6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379) │
│ demo    │ 10                           │ clangsa (3/3), clang-tidy (3/3), cppcheck (3/3) │ 2026-10-24 18:01:19.062743 │ v2          │ 0:00:00  │             │ 6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379) │
└─────────┴──────────────────────────────┴─────────────────────────────────────────────────┴────────────────────────────┴─────────────┴──────────┴─────────────┴─────────────────────────────────────────────────┘
```

`-o plaintext` and `-o table` print the same table. `-o rows` prints one
line per run without borders:

```
 demo 10 clangsa (3/3), clang-tidy (3/3), cppcheck (3/3) 2026-10-24 18:01:19.062743 v2 0:00:00  6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379)
```

"Analyzer statistics" shows `<analyzer> (<total>/<successful>)`: the number of analyzed files, then how many of them succeeded (from the latest store).

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
demo,10,clangsa (3/3), clang-tidy (3/3), cppcheck (3/3),2026-10-24 18:01:19.062743,v2,0:00:00,,6.30 (451b0c5c1786ed21b0685c298f759164a8c5d379)
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
┌────────────────────────────┬──────────┬─────────────┬───────────┬─────────────────────┬─────────────────────────────────────────────────┬─────────────┐
│ Date                       │ Run name │ Version tag │ User      │ CodeChecker version │ Analyzer statistics                             │ Description │
├────────────────────────────┼──────────┼─────────────┼───────────┼─────────────────────┼─────────────────────────────────────────────────┼─────────────┤
│ 2026-10-24 18:01:19.053151 │ demo     │ v2          │ Anonymous │ 6.30 (451b0c5c...)  │ clangsa (3/3), clang-tidy (3/3), cppcheck (3/3) │             │
│ 2026-10-24 17:47:05.550945 │ demo     │ v1          │ Anonymous │ 6.30 (451b0c5c...)  │ clangsa (3/3), clang-tidy (3/3), cppcheck (3/3) │ first run   │
└────────────────────────────┴──────────┴─────────────┴───────────┴─────────────────────┴─────────────────────────────────────────────────┴─────────────┘
```

"User" is `Anonymous` on servers without authentication.

⚠️ If the `-n` pattern matches no run, the history of **all** runs is printed
(verified). At most the 500 newest entries are shown.

Exit code: `0` (`1` on connection or authentication errors).
