# Upload results to a server (`CodeChecker store`)

`CodeChecker store` packs one or more report folders (with the source files
the reports refer to) into a zip, uploads it to a product on a running server,
and saves it as a **run**. Storing again under the same run name updates the
run and records a new entry in its history.

```
CodeChecker store [FOLDER ...] [-n NAME] [--tag TAG] [--description TEXT]
                  [--trim-path-prefix PREFIX ...] [-j JOBS] [--zip-loc PATH]
                  [--detach] [-f] [--config FILE] [--url PRODUCT_URL] [-t plist]
```

`FOLDER` defaults to `~/.codechecker/reports`. `--url` defaults to
`localhost:8001/Default`, in the format `[http[s]://]host:port/ProductEndpoint`.

## Store a run ✅

```sh
CodeChecker store ./reports -n demo --tag v1 --description "first run" --url localhost:8555/Default
```

```
[INFO] - Password file not found.
[INFO] - Checking for local valid sessions.
...
[INFO] - Storing the reports finished successfully.
```

- `-n`: run name. If not given, the name from `analyze -n` is used.
- `--tag`: a label for this store in the run history, e.g. a version. Used in
  `cmd diff -b run:tag`, `cmd results --tag` and `cmd history`.
- `--description`: shown with the run.

## Store an update of the same run ✅

```sh
CodeChecker store ./reports2 -n demo --tag v2 --url localhost:8555/Default
```

Reports in the run get a **detection status**: `new`, `unresolved` (seen
before), `resolved` (gone in this store), `reopened`, `off` (checker was
turned off), `unavailable` (checker no longer exists). See
[cmd-results.md](cmd-results.md).

⚠️ The help text says that without `-f`, reports from files that were not
analyzed this time are kept. In 6.30.0 this is **not** the case (verified):
every report of the run that is not in the new upload gets detection status
`resolved` (or `off`/`unavailable`, see [cmd-results.md](cmd-results.md)).
Storing a folder with only `main.c` results into a run that also had
`style.cpp` reports marks all `style.cpp` reports `RESOLVED`. Always store
the complete result set of the project into a run.

## Replace the run completely (`-f`) ✅

```sh
CodeChecker store ./reports2 -n demo2 -f --url localhost:8555/Default
```

Deletes everything in the run first, then stores only the given results.

## Shorten stored paths ✅

```sh
CodeChecker store ./reports2 -n demo --trim-path-prefix "$HOME/demo2/" --url localhost:8555/Default
```

Paths on the server become `main.c` instead of `/home/user/demo2/main.c`.

⚠️ Source component patterns such as `*/main.c` need a `/` before the name,
so they no longer match trimmed paths like `main.c` (see
[cmd-components.md](cmd-components.md)).

## Store several folders as one run ✅

```sh
CodeChecker store ./reports_a ./reports_b -n multi --url localhost:8555/Default
```

With several folders, the `off` and `unavailable` detection statuses are not
computed.

## Read-only report folder (`--zip-loc`) ✅

```sh
mkdir -p /tmp/zipdir
CodeChecker store ./reports -n demo2 --zip-loc /tmp/zipdir --url localhost:8555/Default
```

The temporary zip is written to `/tmp/zipdir` instead of the report folder.
⚠️ The folder must already exist; otherwise `store` crashes with
`FileNotFoundError: [Errno 2] No such file or directory: '/tmp/zipdir/tmpXXXX-store'`.

## Number of jobs ✅

```sh
CodeChecker store ./reports -n demo -j 1 --url localhost:8555/Default
```

Processes used to read the report folder. `-j 1` uses no subprocesses; this
helps on NFS, where parallel reading can hang.

## Don't wait for the server (`--detach`) ✅

```sh
CodeChecker store ./reports -n detached --detach --url http://localhost:8555/Default
```

```
...
	CodeChecker cmd serverside-tasks --token 8a19ac12a05d...5c75d --await
8a19ac12a05d87f64abb39e4c9852c2ee61cb218b159b41ef1723f965d45c75d
```

`store` exits as soon as the server has accepted the upload. The server
processes it in the background. The last line is the task token; follow the
task with `cmd serverside-tasks` (see
[cmd-serverside-tasks.md](cmd-serverside-tasks.md)).

## Different products and HTTPS ✅

```sh
CodeChecker store ./reports -n p2run --url localhost:8555/proj2
SSL_CERT_FILE=cert.pem CodeChecker store ./reports -n tlsrun --url https://localhost:8562/Default
```

The product must exist:

```
[ERROR] - Internal server error: The product with the given endpoint 'NoSuchProduct' does not exist!
```
(exit code 1)

## Store results from other tools ✅

Folders created by `report-converter` can be stored the same way (see
[report-converter.md](report-converter.md)):

```sh
CodeChecker store ./asan_results -n asan_run --url localhost:8555/Default
```

## Authentication

With an authenticated server, log in first (`cmd login`, which can read
`CC_PASSWORD`), or provide credentials through the password file
(`CC_PASS_FILE`); the session is kept in `CC_SESSION_FILE` (see
[server-authentication.md](server-authentication.md)).

## Exit codes

`0` on success, `1` on any error.
