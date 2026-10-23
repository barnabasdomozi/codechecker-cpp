# Background tasks on the server (`cmd serverside-tasks`)

Long operations, currently mainly storing results (`massStoreRunAsynchronous`),
run as background tasks on the server. Each task has a token. This command
queries tasks, waits for them, and (for superusers) cancels them. It takes a
**server** URL (`host:port`).

```
CodeChecker cmd serverside-tasks [-t TOKEN ...] [--await] [--kill]
                                 [--output {plaintext,table,json}] [filters]
                                 [--url SERVER_URL]
```

Who can see what:
- Anyone can query a task by its token if they created it.
- `PRODUCT_ADMIN`s can list tasks of their products.
- `SUPERUSER`s can list all tasks and cancel them.

## Query one task by token ✅

```sh
CodeChecker cmd serverside-tasks -t 8a19ac12a05d87f64abb39e4c9852c2ee61cb218b159b41ef1723f965d45c75d --url localhost:8555
```

```
Task '8a19ac12a05d87f64abb39e4c9852c2ee61cb218b159b41ef1723f965d45c75d':
    - Type:         report_server::massStoreRunAsynchronous()
    - Summary:      Store of results to 'Default' - 'detached'
    - Status:       COMPLETED
    - Product:      Default
    - User:         Anonymous
    - Enqueued at:  2026-10-23 21:40:02
    - Started at:   2026-10-23 21:40:02
    - Last seen:    2026-10-23 21:40:02
    - Completed at: 2026-10-23 21:40:02
```

Times are UTC. With exactly one `-t` and no `--await`/`--kill`, the exit code
tells the task state:

| Code | Meaning |
|---|---|
| 0 | Completed successfully |
| 1 | Operational error (reserved) |
| 2 | Command-line error (reserved) |
| 4 | The task failed |
| 8 | Still running |
| 16 | Cancelled, or the server shut down |

## Wait for a task ✅

```sh
CodeChecker cmd serverside-tasks -t <TOKEN> --await --url localhost:8555
```

Blocks until the task ends. Exit code `0` if it completed (or was killed with
`--kill`), non-zero otherwise. Use this after `store --detach`.

## List tasks with filters ✅

```sh
CodeChecker cmd serverside-tasks --status completed --product Default --output table --url localhost:8555
CodeChecker cmd serverside-tasks --type "report_server::massStoreRunAsynchronous()" --output json --url localhost:8555
CodeChecker cmd serverside-tasks --enqueued-after 2020:01:01 --no-cancelled --output json --url localhost:8555
```

```json
[{"machineId": "ai-house:8555", "statusConsumed": false,
  "token": "1211a1c332a30b6d2843a97a230beb1abb834f8237bc2109ab6dcd907e9d7fe2",
  "taskKind": "report_server::massStoreRunAsynchronous()", "actorUsername": ..., ...}]
```

A token or at least one filter is required ✅:

```
[ERROR] - ERROR! To use 'CodeChecker cmd serverside-tasks', a '--token' list or some other filter criteria **MUST** be specified!
```

Filters (values of one filter are OR-ed, different filters are AND-ed):

| Filter | Values |
|---|---|
| `--machine-id ID ...` | Server instance (see `server --machine-id`) |
| `--type TYPE ...` | Exact task kind, e.g. `report_server::massStoreRunAsynchronous()` |
| `--status ...` | `allocated`, `enqueued`, `running`, `completed`, `failed`, `cancelled`, `dropped` |
| `--username USER ...` / `--no-username` | Who created it / no user |
| `--product ENDPOINT ...` / `--no-product` | Product / server-wide tasks (SUPERUSER only) |
| `--enqueued-before/after`, `--started-before/after`, `--finished-before/after`, `--last-seen-before/after` | `year:month:day[:h:m:s]`, UTC |
| `--only-cancelled` / `--no-cancelled` | Received a kill request or not |
| `--only-consumed` / `--no-consumed` | The creator has / has not checked the result |

⚠️ `--type` must match exactly; `--type TaskInfo` returned `[]`.

A user without listing rights gets an empty table (tested as `bob` on the
authenticated server).

## Cancel tasks ✅

```sh
CodeChecker cmd serverside-tasks --status running --kill --url localhost:8555
```

Sends a cancel request for every matching task (SUPERUSER only; on a server
without authentication everyone is superuser). Cancellation is cooperative:
tasks may finish anyway, and it may take time. A task that has not started yet
is dropped. In the test, the matched tasks were already completed, so the
command only listed them.
