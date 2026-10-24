# Reference: `CodeChecker server`

Usage: [../usage/server.md](../usage/server.md)

Python source: `web/server/codechecker_server/cli/server.py` (arguments,
`__handle`, `server_init_start`, instance management),
`web/server/codechecker_server/server.py` (`start_server`,
`CCSimpleHttpServer`, process model, signals),
`web/server/codechecker_server/session_manager.py` (`server_config.json`),
`web/server/codechecker_server/instance_manager.py`,
`web/server/codechecker_server/product.py`,
`web/config/server_config.json` (template).

Related: HTTP layer and API in [server-api.md](server-api.md); databases,
schema and the start-up database checks in
[server-database.md](server-database.md); authentication in
[server-authentication.md](server-authentication.md); background tasks in
[cmd-serverside-tasks.md](cmd-serverside-tasks.md).

## 1. Command line

```
CodeChecker server [-w WORKSPACE] [-f CONFIG_DIRECTORY] [--machine-id ID]
                   [--api-handler-processes N] [--task-worker-processes N]
                   [--host ADDRESS | --not-host-only] [-v/-p/--view-port/--port PORT]
                   [--skip-db-cleanup] [--config FILE]
                   [--sqlite SQLITE_FILE | --postgresql]
                   [--db-host/--dbaddress H] [--db-port/--dbport P]
                   [--db-username/--dbusername U] [--db-name/--dbname N]
                   [--force-authentication]
                   [-l | -r | -s | --stop-all]
                   [--db-status PRODUCT | --db-upgrade-schema PRODUCT | --db-force-upgrade]
                   [--verbose L]
```

| Option | Default | Notes |
|---|---|---|
| `-w` | `~/.codechecker` | workspace |
| `-f` | = workspace | config directory (`server_config.json`, `server_secrets.json`, `cert.pem`, `key.pem`) |
| `--machine-id` | `<hostname>:<port>` | absent unless given |
| `--api-handler-processes`, `--task-worker-processes` | none | override `server_config.json` |
| `--host` | `localhost` | listen address |
| `--not-host-only` | | same as `--host "::"` |
| port | `8001` | `0` = the OS picks a free port |
| `--sqlite` | `<workspace>/config.sqlite` | made absolute against the current directory |
| `--postgresql` | | config DB on PostgreSQL; the `--db-*` defaults are `localhost`, `5432`, `codechecker`, `config` |

Groups: `--sqlite`/`--postgresql` mutually exclusive; `-l`/`-r`/`-s`/`--stop-all`
mutually exclusive; `--db-status`/`--db-upgrade-schema`/`--db-force-upgrade`
mutually exclusive (so `--db-upgrade-schema X --db-force-upgrade` is an
argparse error).

Extra checks in the handler (looking at the raw `argv`, because defaults
cannot be told apart from given values):
- any of `--dbaddress --dbport --dbusername --dbname --db-host --db-port
  --db-username --db-name` without `--postgresql` →
  `argument <first one>: not allowed without argument --postgresql`;
- `--not-host-only` together with `--host` →
  `argument --not-host-only: not allowed with argument --host, as it is a shortcut to --host "::"`.

Both are parser errors: usage on stderr, exit 1
([common-infrastructure.md](common-infrastructure.md) §4).

Then the handler logs INFO `Workspace directory: <w>` and
`Config directory: <f>` (paths as given) and calls `main`.

`--config FILE` reads the `server` section
([common-infrastructure.md](common-infrastructure.md) §5).

## 2. `main`

1. Unless in instance-manager mode (`-l`, `-s`, `--stop-all`): create the
   workspace (INFO `Creating non existing workspace directory: <w>`) and the
   config directory (INFO `Creating non existing config directory: <f>`) if
   missing.
2. Set up logging (stdout); if `CC_LOG_CONFIG_PORT` is set, start a
   log-configuration listener on that port (optional for a port).
3. `--config` given but missing → ERROR `Configuration file '<f>' does not
   exist.`, exit 1.
4. Instance management (§3) or server start (§4).

## 3. Instance management

Running servers are recorded in `~/.codechecker.instances.json` (always in
the home directory, not configurable): a JSON array of
`{"pid", "hostname", "workspace" (absolute), "port"}`, indented by 2. The
file is created with mode 0600 and every rewrite holds an exclusive file
lock. Records are added when a server starts (after the HTTP socket is
bound) and removed at its exit. On every rewrite, records with the same
pid as a new one, or matching a removed `hostname:pid`, are dropped.

Reading (`get_instances`) filters out dead records: a record of **this**
host is alive if a process with that pid exists, its `argv[1]` ends with
`codechecker_common/cli.py` or `CodeChecker` and it belongs to the current
user. Records of other hosts are always kept (shared home directories).

### 3.1 `-l/--list`

Prints `Your running CodeChecker servers:` and a `table`
([common-infrastructure.md](common-infrastructure.md) §7) with
`Workspace, View port`. If any record is from another host, the header is
`Workspace, Computer host, View port` and the host column is empty for this
host. Exit 0.

### 3.2 `-s/--stop`, `--stop-all`

For every live record of **this** host (`--stop`: only the one whose port
equals the given port and whose absolute workspace equals the absolute
`-w`): send `SIGTERM` to the pid, wait up to 5 s for its direct children and
`SIGKILL` the remaining ones, then wait (1 s steps, up to ~11 s) for the
main process; on timeout WARNING `Waiting for process <pid> to stop has been
timed out(timeout = 10)! Process is still running!`. INFO
`Stopped CodeChecker server running on port <p> in workspace <w> (PID: <pid>)`.
If signalling fails: ERROR `Couldn't stop process PID #<pid>` and the
exception propagates (traceback, exit 1). No matching server → nothing
printed, exit 0.

### 3.3 `-r/--reload`

For each live record of this host with the given port and workspace, send
`SIGHUP` (§6). Exit 0 (also when nothing matched). Note: `-r` is not in
instance-manager mode, so the workspace and config directory are created
first if missing.

## 4. Starting the server (`server_init_start`)

1. Check zlib is available (else `ModuleNotFoundError`).
2. Create the directory of the SQLite config DB file if missing.
3. `--force-authentication` → INFO `'--force-authentication' was passed as a
   command-line option. The server will ask for users to authenticate!`
4. Configuration database check, `--db-status`/`--db-upgrade-schema`, the
   `Default` product, product status table and upgrades:
   [server-database.md](server-database.md) §3.
5. Package data: web root `<package>/www`, version = git describe tag
   ([server-api.md](server-api.md) §2.4).
6. `start_server` (§5). A bind failure with `EADDRINUSE` →
   ERROR `Couldn't start the server: [Errno 98] Address already in use`,
   ERROR `Server can't be started, maybe port number (<p>) is already used.
   Check the connection parameters. Use the option '-p 0' to find a free
   port automatically.`, exit 1. Other socket errors propagate.

## 5. `start_server`

1. `<config dir>/root.user` exists → WARNING that it is deprecated and
   ignored (use `super_user` in `server_config.json`).
2. `<config dir>/server_config.json` missing: if
   `<config dir>/session_config.json` exists, rename it (INFO `Renaming ...`);
   otherwise copy the package template `config/server_config.json` and log
   INFO `CodeChecker server's example configuration file created at '<file>'`.
3. Unless `--skip-db-cleanup`: garbage-collect all product databases in
   parallel ([server-database.md](server-database.md) §5).
4. Drop background tasks left behind by a previous server with the same
   machine id and delete task temporary data
   ([cmd-serverside-tasks.md](cmd-serverside-tasks.md)); if any were dropped:
   INFO `At server startup, dropped <n> background tasks left behind by a
   previous server instance matching machine ID '<id>'.`
5. Create the HTTP server object:
   - INFO `Server API version: 6.74`;
   - read `server_config.json` (§7; an unreadable file → ERROR `The server's
     configuration file is missing or can not be read!`, a bad file → ERROR
     with the message, exit 1);
   - unless `--skip-db-cleanup`: delete expired sessions from the config DB
     (INFO `Starting garbage collection on the config database ...` /
     `Finished garbage collection on the config database.`);
   - initialise default permission records for the system scope and every
     product ([server-authentication.md](server-authentication.md) §5);
   - connect every product (§8);
   - bind the socket (IPv6 socket if the listen address contains `:`;
     listen backlog 511); `SO_REUSEADDR` is set (Python `HTTPServer`);
   - keepalive (§7) and TLS (§5.1).
6. Register the instance (§3).
7. Start `N_api` API handler processes and `N_task` task worker processes
   (§6); INFO `Using <N_api> API request handler processes ...`,
   `Using <N_task> Task worker processes ...`.
8. INFO `Server waiting for client requests on [<address>:<port>]`
   (`[[<addr>]:<port>]` for IPv6, e.g. `[[::]:8559]`). With port `0`, the
   real port is printed.
9. Wait (polling every 5 s) until shutdown is requested; then drop the
   unfinished tasks of this machine id (INFO `At server shutdown, dropped
   <n> background tasks that will never be completed.`), INFO `CodeChecker
   server quit (main process).` and return the exit code (§6).

### 5.1 TLS

If both `<config dir>/key.pem` and `<config dir>/cert.pem` exist: INFO
`Initiating SSL. Server listening on secure socket.`; TLS server context
with the certificate chain, TLS 1.0 and 1.1 disabled. Otherwise INFO
`Searching for SSL key at <key>, cert at <cert>, not found!` and
`Falling back to simple, insecure HTTP.` There is no option to name other
files.

## 6. Processes and signals

The main process only supervises; it does not answer requests.

- **API handler processes**: each runs the accept loop on the shared
  listening socket (pre-fork model) and handles one request at a time.
- **Task worker processes**: run background tasks (store) from a shared
  queue ([cmd-serverside-tasks.md](cmd-serverside-tasks.md)).

Children are started with default signal handlers. Main process signals:

| Signal | Action |
|---|---|
| `SIGINT`, `SIGTERM` (first) | INFO `Shutting down the WEB server on [<addr>] ... Please allow some time for graceful clean-up!`; set the shared "shutting down" flag; exit code := `128 + signum`; send `SIGINT` to every API process and wait for it; close the task queue; send `SIGHUP` to every task process and wait for it. |
| `SIGINT`, `SIGTERM` (again, > 2 s after the first) | WARNING `Termination signal <...> received a second time, **FORCE** killing the WEB server on [<addr>] ...`; `SIGKILL` all children; exit `128 + signum`. |
| `SIGHUP` | INFO `Received signal to reload server configuration ...`, reload (§7.2), INFO `Server configuration reload: Done.` |
| `SIGCHLD` | Restart dead children so the requested numbers stay alive: WARNING `'<api|background>' child process (PID <pid>, "<name>") is not alive anymore!`, INFO `(Re-)starting <n> '<kind>' child process(es) ...`. Ignored during shutdown. Every `min(N_api, N_task) // 2` events a full check of all children is forced (WARNING `Too many children died since last full status check, performing one ...`). |

An API process receiving `SIGINT` closes the socket and exits with
`128 + 2`.

Exit codes of `CodeChecker server`: `0` for instance management and
reload; `128 + signum` after a signal-initiated stop (`143` for `SIGTERM`,
`130` for `SIGINT`); `1` for errors; the database paths in
[server-database.md](server-database.md) §3 have their own codes.

⚠️ The reload (`SIGHUP`) is only applied in the **main** process. The API
handler processes are forked at start and keep the configuration they
inherited, so a reload has no effect on request handling. Verified: after
setting `max_run_count: 1` and `CodeChecker server -r`, a second run could
still be stored; a server *started* with that value rejects it with
`You reached the maximum number of allowed runs (1/1)! ...`. The usage page
describes the intended behavior; a port should apply the reload where
requests are served.

## 7. `server_config.json`

Read at start (and at reload) from the config directory. Must be a JSON
object; empty or invalid → `Server configuration file was invalid, or
empty.` (exit 1). The server warns if group/other can read or write it
(same message as [server-api.md](server-api.md) §4.2).

### 7.1 Variable substitution

Optional `<config dir>/server_secrets.json` (a JSON object, permission
warning too). Every **string value** anywhere in the config (recursively,
also in lists) that is exactly `$SECRET:<name>$` or `$ENV:<name>$`
(`<name>` = `[a-zA-Z0-9_-]+`) is replaced by `secrets[<name>]` or the
environment variable. Unresolvable → ERROR `Secrets were used in server
configuration file, but <secrets file> does not exist!` (if applicable) and
`Variable '<value>' could not be resolved in server configuration file.`
(exit 1 at start).

### 7.2 Keys

| Key | Default in template | Meaning |
|---|---|---|
| `worker_processes` | `null` | API handler processes |
| `background_worker_processes` | `null` | task worker processes |
| `max_run_count` | `null` | max runs per product, `null` = unlimited ([store.md](store.md)) |
| `store.analysis_statistics_dir` | `null` | where uploaded analysis statistics zips are kept; `null` = not kept |
| `store.limit.failure_zip_size` | `52428800` | bytes ([store.md](store.md)) |
| `store.limit.compilation_database_size` | `104857600` | bytes ([store.md](store.md)) |
| `keepalive.enabled` / `idle` / `interval` / `max_probe` | `false` / `600` / `30` / `10` | TCP keepalive |
| `authentication` | | [server-authentication.md](server-authentication.md) |

Process counts (`get_worker_processes`): default = number of CPUs. If a
value is missing, `null`, `0` or negative: WARNING `Number of worker
processes can not be negative! Default value will be used: <cpus>` (resp.
`Number of task worker processes can not be negative! Default value will be
used: <value>`); the task worker default is the (corrected) API worker
count. Because the template has `null`, these warnings appear on every
default start. Command-line options override afterwards (even `0`, which
starts no workers).

Keepalive: only if `enabled` is true: `SO_KEEPALIVE` on the listening
socket, and `TCP_KEEPIDLE`, `TCP_KEEPINTVL`, `TCP_KEEPCNT` from `idle`,
`interval`, `max_probe` if truthy.

Reload (`SIGHUP`): re-reads and re-resolves the file. Updates
`max_run_count`, the whole `store` object, and the authentication fields
`session_lifetime`, `refresh_time`, `logins_until_cleanup` (only those
already present; a missing new value becomes `0`). Other keys (methods,
users, processes, keepalive) need a restart. A bad file on reload logs
ERROR `Couldn't reload server configuration file` and keeps the old values.

## 8. Products at run time

Each product is a row of the config DB `products` table with an SQLAlchemy
connection string ([server-database.md](server-database.md) §2). The server
keeps one connection object per product endpoint:

- `connect()`: build the engine, run `SELECT 1`, then check the schema →
  `db_status`. Errors → ERROR `The database for product '<ep>' cannot be
  connected to:\n<error>` and `FAILED_TO_CONNECT`.
- At start every product is connected. Products that fail to connect are
  not kept in memory (requests to them load and retry, see
  [server-api.md](server-api.md) §2.2). For every connected product,
  `num_of_runs` in the config DB is reset to the real number of runs.
- `get_details()` (product list): number of runs, names of runs with a run
  lock, date of the most recently updated run.

## 9. Request handling summary

See [server-api.md](server-api.md) §2. Each request is logged at INFO:
`<client>:<port> -- [<user>] POST <path>@<method>`.
