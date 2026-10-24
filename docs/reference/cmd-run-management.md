# Reference: `CodeChecker cmd update` and `cmd del`

Usage: [../usage/cmd-run-management.md](../usage/cmd-run-management.md)

Python source: `web/client/codechecker_client/cli/cmd.py`
(`__register_update`, `__register_delete`),
`web/client/codechecker_client/cmd_line_client.py` (`handle_update_run`,
`handle_remove_run_results`, `check_run_names`,
`process_run_filter_conditions`, `str_to_timestamp`),
`web/server/codechecker_server/api/report_server.py` (`updateRunData`,
`removeRun`, `get_run_ids_for_filter`, `check_remove_runs_lock`,
`process_run_filter`).

Both commands take `--url PRODUCT_URL` (default `localhost:8001/Default`),
log to stdout, and use the results client
([server-api.md](server-api.md) §6). Both need `PRODUCT_STORE`.

## 1. `cmd update`

```
CodeChecker cmd update RUN_NAME -n/--name NEW_RUN_NAME [--url PRODUCT_URL]
```

`-n` is required.

1. Empty new name → ERROR `The new run name can not be empty!`, exit 1.
2. Look up `RUN_NAME` exactly: `getRunData(RunFilter(names=[RUN_NAME],
   exactMatch=true))` (paged). Not found → WARNING `The following runs were
   not found in the database: <name>.`, exit 1.
3. `updateRunData(runId, NEW_RUN_NAME)`; an error → ERROR `<message>`,
   exit 1. Success → INFO `Done.`, exit 0.

Server `updateRunData(runId, newName)`:
- empty name → `GENERAL "No new run name was given to update the run."`;
- a run with exactly that name exists → `DATABASE "New run name '<n>'
  already exists."`;
- unknown id → `DATABASE "Run id <id> was not found in the database."`;
- else rename (`runs.name`), INFO `Run name '<old>' (<id>) was changed to
  <new> by '<user>'.`, return true.

Only the run row changes; history, reports and tags stay.

## 2. `cmd del`

```
CodeChecker cmd del (-n RUN_NAME [RUN_NAME ...] | --all-before-run RUN_NAME
                     | --all-after-run RUN_NAME | --all-before-time TS
                     | --all-after-time TS) [--url PRODUCT_URL]
```

Exactly one selector is required. `TS` =
`year:month:day[:hour[:minute[:second]]]`, local time.

Run filter (`process_run_filter_conditions`):

| Option | `RunFilter` |
|---|---|
| `-n N...` | `names = N...`, `exactMatch = false` |
| `--all-after-run R` / `--all-before-run R` | `afterRun` / `beforeRun` |
| `--all-after-time TS` / `--all-before-time TS` | `afterTime` / `beforeTime` = local `mktime` of TS |

Matching is done by the server ([cmd-runs-history.md](cmd-runs-history.md)
§1.1): `-n` values are **case-insensitive patterns with `*` as wildcard**
(verified: `-n 'GEN*'` deleted `gen_name`); `before/afterRun` use the
date of the named run (an unknown run name means no condition, i.e.
**all runs**); times compare with `runs.date` (the latest store).

Client: `removeRun(null, filter)`; true → INFO `Done.`; false → WARNING
`No runs were removed, check if the given criteria match existing run
names.` Exit 0 in both cases; errors exit 1. No confirmation is asked.

Server `removeRun(runId, runFilter)`:
1. Filter = `runFilter`, or `RunFilter(ids=[runId])` if none.
2. Resolve the matching run ids now. If any of them has a run lock newer
   than 30 minutes (a store in progress) → `DATABASE "Can not remove results
   because the following runs are locked: <names>"` and nothing is deleted.
3. Delete each resolved run in its own transaction (database cascades
   remove its reports, path data, annotations, histories, statistics and
   analysis links). An exception for one run is logged as WARNING
   `Suppressed an exception while deleting run <name>. Error: <e>` and the
   others continue.
4. INFO `Runs '<n1>', '<n2>' were removed by '<user>'.`; config DB
   `num_of_runs -= <deleted>`.
5. Garbage-collect unused comments and unused analysis info
   ([server-database.md](server-database.md) §5 steps 3–4). Files and file
   contents are left for the next start-up clean-up.
6. Return true iff at least one run matched.

Comments and review status rules are keyed by report hash, not by run;
after step 5 comments survive if another run still has a report with that
hash, and rules are never removed by `del`.
