# Run a CodeChecker server (`CodeChecker server`)

The server stores analysis results in a database and serves them to the web UI
and to the `CodeChecker cmd` / `store` clients over a Thrift API on HTTP(S).

Concepts:
- **Workspace** (`-w`, default `~/.codechecker`): where the server keeps its
  databases.
- **Config directory** (`-f`, default: the workspace): where it reads
  `server_config.json`, `cert.pem` and `key.pem`.
- **Configuration database**: server-wide settings (products, permissions,
  sessions, tokens). SQLite `WORKSPACE/config.sqlite` by default.
- **Product**: a separate result database with its own URL endpoint. On first
  start, a product `Default` is created (`WORKSPACE/Default.sqlite`). See
  [cmd-products.md](cmd-products.md).

Database migration options are on [server-database.md](server-database.md)
and authentication on [server-authentication.md](server-authentication.md).

## Start a server ✅

```sh
CodeChecker server -w ./ws -v 8555
```

```
[INFO] - Workspace directory: ./ws
[INFO] - Config directory: ./ws
[INFO] - Checking configuration database ...
[INFO] - CodeChecker server's example configuration file created at './ws/server_config.json'
[INFO] - Database is up to date.
...
[INFO] - Server API version: 6.74
[WARNING] - './ws/server_config.json' is readable by users other than you! ...
Please 'chmod 0600 ./ws/server_config.json' so only you can access the file.
[INFO] - Searching for SSL key at ./ws/key.pem, cert at ./ws/cert.pem, not found!
[INFO] - Falling back to simple, insecure HTTP.
[INFO] - Using 12 API request handler processes ...
[INFO] - Using 12 Task worker processes ...
[INFO] - Server waiting for client requests on [localhost:8555]
```

The server runs in the foreground until stopped (Ctrl-C, `--stop`, or a
signal). The default port is `8001` (`-v`, `-p`, `--view-port` and `--port`
are the same option). By default it listens only on `localhost`.

## Listen on other addresses ✅

```sh
CodeChecker server -w ./ws -v 8555 --host 127.0.0.1
CodeChecker server -w ./ws -v 8555 --host ::1             # IPv6
CodeChecker server -w ./ws -v 8559 --not-host-only        # every interface, same as --host "::"
```

```
[INFO] - Server waiting for client requests on [[::]:8559]
```

## Separate config directory and config database file ✅

```sh
CodeChecker server -w ./ws2 -f ./cfg2 --sqlite ./ws2/my_config.sqlite -v 8557
```

`server_config.json` is created in `./cfg2`. The configuration database is
`./ws2/my_config.sqlite`. Product databases stay in the workspace.

## PostgreSQL configuration database ❌

```sh
CodeChecker server -w ./ws --postgresql --db-host localhost --db-port 5432 \
  --db-username codechecker --db-name config
```

PostgreSQL arguments are ignored unless `--postgresql` is given. Defaults:
host `localhost`, port `5432`, user `codechecker`, database `config`. The
password comes from a libpq `pgpass` file (`~/.pgpass`, or the file named by
`PGPASSFILE`), e.g. `*:5432:*:codechecker:my_password`. Not tested: no
PostgreSQL server was available.

## Process counts and machine ID ✅

```sh
CodeChecker server -w ./ws2 -v 8557 --api-handler-processes 2 --task-worker-processes 1 \
  --machine-id test-machine
```

```
[INFO] - Using 2 API request handler processes ...
[INFO] - Using 1 Task worker processes ...
```

- `--api-handler-processes`: processes that answer API calls.
- `--task-worker-processes`: processes that run background tasks such as
  storing results.
- These override `worker_processes` and `background_worker_processes` in
  `server_config.json`.
- `--machine-id`: names this server instance for task bookkeeping (see
  [cmd-serverside-tasks.md](cmd-serverside-tasks.md)). The default is
  `<hostname>:<port>`, e.g. `myhost:8555`. Set it when the same server runs
  in changing environments such as containers.

## Skip database cleanup at start ✅

```sh
CodeChecker server -w ./ws -v 8555 --skip-db-cleanup
```

Normally the server removes unused files and data from the databases at
start ("Garbage collection").

## HTTPS ✅

Put `cert.pem` and `key.pem` into the config directory:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -keyout ./ws4/key.pem -out ./ws4/cert.pem \
  -days 365 -subj "/CN=localhost"
CodeChecker server -w ./ws4 -v 8562
```

```
[INFO] - Initiating SSL. Server listening on secure socket.
[INFO] - Server waiting for client requests on [localhost:8562]
```

Clients use `https://` URLs. A self-signed certificate must be trusted
through `SSL_CERT_FILE`:

```sh
SSL_CERT_FILE=./ws4/cert.pem CodeChecker cmd products list --url https://localhost:8562
SSL_CERT_FILE=./ws4/cert.pem CodeChecker store reports -n tlsrun --url https://localhost:8562/Default
```

Without it:

```
[ERROR] - Connection failed.
[ERROR] - OS Error: [SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed: self-signed certificate (_ssl.c:1029)
[ERROR] - Check if your CodeChecker server is running.
```

`REQUESTS_CA_BUNDLE` has **no** effect.

## Options from a file ✅

```sh
CodeChecker server -w ./ws --config server.yaml
```

See [config-file.md](config-file.md).

## List running servers ✅

```sh
CodeChecker server -l
```

```
Your running CodeChecker servers:
┌──────────────────────┬───────────┐
│ Workspace            │ View port │
├──────────────────────┼───────────┤
│ /home/user/ws        │ 8555      │
│ /home/user/ws2       │ 8559      │
└──────────────────────┴───────────┘
```

Running servers are recorded in `~/.codechecker.instances.json`.

## Reload configuration ✅

```sh
CodeChecker server -w ./ws -v 8555 -r
```

Sends `SIGHUP` to the server for that workspace and port. The server re-reads
`server_config.json` (e.g. authentication settings) without restarting.

## Stop servers ✅

```sh
CodeChecker server -w ./ws2 -v 8559 -s    # the server for this workspace and port
CodeChecker server --stop-all             # every server started by you
```

```
[INFO] - Stopped CodeChecker server running on port 8559 in workspace ./ws2 (PID: 30654)
```

## `server_config.json`

Created with defaults on first start. Main keys:

| Key | Purpose |
|---|---|
| `worker_processes`, `background_worker_processes` | Process counts. `null` = number of CPUs. |
| `max_run_count` | Maximum number of runs per product (`null` = no limit) |
| `store.analysis_statistics_dir` | Folder for analysis statistics uploaded by `store` (`null` = not kept) |
| `store.limit.failure_zip_size` | Max size of uploaded failure zips (default 52428800 bytes) |
| `store.limit.compilation_database_size` | Max size of an uploaded compilation database (default 104857600 bytes) |
| `keepalive` | TCP keepalive: `enabled` (false), `idle` (600), `interval` (30), `max_probe` (10) |
| `authentication` | See [server-authentication.md](server-authentication.md) |

⚠️ On start the server logs "Number of worker processes can not be negative!
Default value will be used: 12" even for the `null` default.

The server warns when this file is readable by other users.

## Exit codes

`0` after a clean stop. When stopped by a signal, the shell reports
`128+signal` (e.g. `143` for `SIGTERM`).
