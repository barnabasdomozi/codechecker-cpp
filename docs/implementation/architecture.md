# Architecture of the C++ port

Status: **accepted** (2026-10-25).

This page describes how the C++ port is structured: the source tree, the
libraries it is split into, the programs that are installed, the processes
and threads at run time, and how each major part works. It builds on the
project-wide decisions in [README](README.md) (C++20, CMake, Linux only, no
Python code, copied web UI, ldlogger and Thrift IDL files) and on the library choices in
[libraries.md](libraries.md). How the Python version behaves is described
in [../reference/](../reference/README.md); this page refers to it instead of
repeating it.

## Design goals

1. **Same behavior as the Python version.** Same command line, output, files
   and exit codes. The black-box suite in
   [blackbox_tests/](../../blackbox_tests/README.md) is the judge.
2. **Compatible with the Python version on the outside.** The parts can be
   work together with Python ones, but only through the network and files;
   neither version ever starts the other's programs:
   - Thrift on the wire: Python clients work with the C++ server, the C++
     client works with a Python server, and the copied web UI works with the
     C++ server;
   - the same files on disk: report folders, `~/.codechecker.*.json`, server
     workspaces;
   - the same databases, at the latest schema.
3. **The structure follows the Python packages.** Each C++ module says which
   Python module it ports, so the reference page and the Python source for any
   piece of code are easy to find.
4. **The analyzer side stands alone.** `analyze`, `parse` and the local tools
   build and run without the client and server libraries (rule 5 of
   [libraries.md](libraries.md#how-we-decide)).
5. **Port one component at a time** (see [Porting order](#14-porting-order)).

## 1. Overview

```
  build command
       │  LD_PRELOAD=ldlogger.so          (CodeChecker log)
       ▼
  compile_commands.json
       │                                  (CodeChecker analyze)
       │  runs clang, clang-tidy, cppcheck, gcc, infer in a thread pool
       ▼
  report folder: *.plist, metadata.json, compile_cmd.json, ...
       │                       │
       │ CodeChecker parse     │ CodeChecker store
       ▼                       ▼
  text, JSON, SARIF,      Thrift over HTTP ──►  CodeChecker server  ──► SQLite / PostgreSQL
  HTML, Code Climate,                              ▲       ▲
  Gerrit, baseline        CodeChecker cmd ─────────┘       │
                          web UI in a browser ─────────────┘
```

The port has the same parts as the Python project:

| Part | Python | C++ |
|---|---|---|
| Build logger | `analyzer/tools/build-logger` (C) | C sources copied unchanged |
| Analyzer commands | `analyzer/codechecker_analyzer` | `src/analyzer/` |
| Report model and formats, `report-converter` | `tools/report-converter` | `src/report/`, `src/tools/report-converter/` |
| Shared helpers | `codechecker_common` | `src/common/` |
| Other local tools | `tools/tu_collector`, `analyzer/tools/*`, `tools/bazel`, `tools/memory-safety-reporter`, `scripts/gerrit_changed_files_to_skipfile.py` | `src/tu_collector/`, `src/tools/*` |
| API definition | `codechecker_api/*.thrift` | copied unchanged, C++ code generated |
| Shared client/server code | `web/codechecker_web` | `src/web/` |
| Command line client | `web/client/codechecker_client` | `src/client/` |
| Server | `web/server/codechecker_server` | `src/server/` |
| Web UI | `web/server/vue-cli` | copied unchanged |

## 2. Source tree

```
codechecker-cpp/
├── CMakeLists.txt
├── cmake/                       CMake modules: finding libraries, Thrift code
│                                generation, version files, install rules
├── api/                         copied from codechecker_api/: *.thrift,
│                                api_version.json
├── config/                      package data files, copied (see below)
├── ldlogger/                    copied from analyzer/tools/build-logger/src/
├── web-ui/                      copied from web/server/vue-cli/
├── data/
│   └── html/                    HTML report templates and static files, copied
│                                from report-converter's report/output/html/static/
├── src/
│   ├── platform/                OS layer (Linux)
│   ├── util/                    Python-compatible building blocks
│   ├── report/                  report model, parsers, output formats
│   ├── common/                  codechecker_common
│   ├── tu_collector/
│   ├── analyzer/                codechecker_analyzer
│   ├── web/                     codechecker_web (client + server shared code)
│   ├── client/                  codechecker_client
│   ├── server/                  codechecker_server
│   ├── launcher/                the codechecker-cpp program: launcher, subcommand table
│   └── tools/                   one folder per standalone tool (main.cc)
├── unit_tests/                  GoogleTest unit tests, one folder per library
├── third_party/                 one git submodule per third-party library, and
│                                the build of these libraries (build-system.md)
├── blackbox_tests/              the black-box suite (already exists)
└── docs/
```

**Copied, not changed.** These come from the reference commit (`451b0c5c`)
as they are. A new version is taken by copying them again, never by editing
them here:

| Folder | Source in the Python repository |
|---|---|
| `api/` | `codechecker_api/*.thrift`, `codechecker_api/api_version.json` |
| `ldlogger/` | `analyzer/tools/build-logger/src/` and its `README.md` (not the `Makefile` or the Python tests) |
| `web-ui/` | `web/server/vue-cli/` with its `package.json`, `package-lock.json`, `.npmrc` and other files, without `node_modules/` and `dist/` |
| `config/` | `config/` (labels, guidelines, `package_layout.json`, `config.json`), `web/config/` (`session_client.json`, `system_comment_kinds.json`, `git_commit_urls.json`), `web/server/config/server_config.json` |
| `data/html/` | `tools/report-converter/codechecker_report_converter/report/output/html/static/` |

None of these contain Python code, as the README requires. A future copy
must be checked for `.py` files and leave them out.

`analyzer_version.json` and `web_version.json` are not copied. The build
writes them ([build-system.md](build-system.md#43-generated-files) §4.3).

Inside `src/`, the file comment of a C++ file that ports a Python module
names that module (for example `// Port of codechecker_common/skiplist_handler.py`).
All C++ code follows the coding guideline [cppguide.md](cppguide.md).
Folder names inside each library follow the Python package where that
helps.

## 3. Libraries and their dependencies

Each `src/` folder is one static library (CMake target). Executables are thin
`main()` functions on top of them.

| Target | Contents | Python counterpart | Third-party libraries |
|---|---|---|---|
| `cc_platform` | processes, signals, `flock`, temporary files, `/proc`, `getpass`, environment, stack traces | `subprocess`, `signal`, `portalocker`, `psutil`, `tempfile` | – |
| `cc_util` | `argparse` clone, logging, output streams, `pypath`, Unicode helpers, `pyre` (regex), JSON reader/writer, YAML reader/writer, plist, `OrderedMap`, `twodim` tables, `shlex`, `fnmatch`, `glob`, `base64`, `semver`, UUIDs, hashes, zlib and ZIP wrappers | standard library and small packages | PCRE2, yyjson, libyaml, libxml2, OpenSSL (libcrypto), zlib, libzip |
| `cc_report` | report model, plist and SARIF parsers, analyzer output converters, output formats, report hash, changed-file detection | `codechecker_report_converter` | – |
| `cc_common` | checker labels, guidelines, skip lists, review status and source code comments, `--config` splicing, package context and version files, shared CLI helpers | `codechecker_common` | – |
| `cc_tu_collector` | collecting translation unit files into a ZIP, dependent sources | `tu_collector` | – |
| `cc_analyzer` | analyzer context, build log handling, compiler info, analyzer drivers, analysis manager, CTU and statistics, the analyzer subcommands | `codechecker_analyzer` | – |
| `cc_api` | code generated from `api/*.thrift`, API version, Thrift JSON helpers | `codechecker_api` | Apache Thrift (+ Boost headers) |
| `cc_web` | code shared by client and server: web version, Thrift ↔ model conversions, enum names, database status texts | `codechecker_web` | – |
| `cc_client` | HTTP transport, credentials and sessions, call wrapper, `store`, `cmd` | `codechecker_client` | libcurl, OpenSSL (libssl) |
| `cc_server` | supervisor and processes, HTTP layer, routing, API handlers, database layer, authentication, background tasks | `codechecker_server` | cpp-httplib, libsqlite3, libpq, jwt-cpp, libldap, libcurl, OpenSSL |

Dependencies between them (direct dependencies only):

```
cc_util          → cc_platform
cc_report        → cc_util
cc_common        → cc_report
cc_tu_collector  → cc_common
cc_analyzer      → cc_common, cc_tu_collector
cc_api           → (generated Thrift code only)
cc_web           → cc_common, cc_api
cc_client        → cc_web
cc_server        → cc_web
```

The **analyzer side** is everything up to `cc_analyzer`; the **web side** is
`cc_api`, `cc_web`, `cc_client` and `cc_server`. The web side uses
`cc_report` through `cc_common`: `store` reads report folders, and the server
parses the uploaded plist files.

The CMake option `CC_BUILD_WEB` (default `ON`) builds the web side. With
`OFF`, the web libraries are not built, their third-party libraries are not
searched for, and `codechecker-cpp` has no `store`, `cmd`, `server` and
`web-version` subcommands. This is the same split as the Python
`analyzer`-only package. A CI build with `CC_BUILD_WEB=OFF` keeps the
analyzer side from picking up a web dependency by accident.

## 4. Programs and install layout

The install tree has the same layout as the Python package
([common-infrastructure.md](../reference/common-infrastructure.md) §1),
because the test suite and users rely on it:

```
<root>/
├── bin/
│   ├── codechecker-cpp                      launcher + all subcommands
│   ├── report-converter
│   ├── tu_collector
│   ├── merge-clang-extdef-mappings
│   ├── post-process-stats
│   ├── memory_safety_reporter
│   ├── bazel-compile-commands
│   ├── gerrit_changed_files_to_skipfile.py  a C++ program with this name
│   └── ldlogger
├── ld_logger/lib/{32bit,64bit}/ldlogger.so
├── lib/codechecker/                         data the Python version keeps inside
│   ├── api_version.json                     its Python packages
│   └── html/                                HTML report templates and static files
├── config/                                  as in the Python package
└── www/                                     the built web UI
```

- There is no `lib/python3/`. Data files that the Python version reads from
  inside its packages are installed under `lib/codechecker/`.
- `gerrit_changed_files_to_skipfile.py` keeps its name, `.py` included, so
  scripts that call it keep working.
- Every standalone tool is its own executable: `src/tools/<tool>/main.cc`
  linking the libraries it needs. These tools use plain `argparse` rules
  (parse errors exit 2); the `argparse` clone takes the error exit code as a
  parser setting.
- `ldlogger` and `ldlogger.so` are compiled by CMake from the copied C
  sources; the Python repository's `Makefile` is not used. The details,
  including the 32-bit library, are in
  [build-system.md](build-system.md#44-ldlogger).

Installed programs find the package root from their own location
(`/proc/self/exe`, then `..`), the same way `CC_DATA_FILES_DIR` is set in
the Python version.

## 5. The `codechecker-cpp` program

The main program is named `codechecker-cpp` (decided 2026-10-25), not
`CodeChecker`. It has the subcommands, options, output and exit codes of the
Python `CodeChecker`; usage and help texts name it `codechecker-cpp`
(`usage: codechecker-cpp version ...`). The other programs keep their names.

### 5.1 Launcher

The Python launcher (`bin/CodeChecker`) starts the real entry point as a child
process ([common-infrastructure.md](../reference/common-infrastructure.md)
§2). The port keeps this two-process shape, but in one executable and
without `exec`:

1. `main()` saves the environment to a temporary file and sets
   `CODECHECKER_ORIGINAL_BUILD_ENV` and `CC_BIN_DIR`.
2. It calls `fork()`. Nothing else has happened yet: there are no threads and
   no open files except the standard streams.
3. The **parent** forwards `SIGINT`, `SIGTERM`, `SIGHUP` and `SIGCHLD` to the
   child, waits for it, deletes the temporary folder and exits with the
   child's exit code.
4. The **child** runs the entry point (§5.2).

Why two processes: the temporary folder is always removed, even when the
command exits through a signal handler, and signal behavior and process ids
match the reference. For example, `server --stop` sends `SIGTERM` to the
recorded pid (the child), and `CodeChecker` exits with `143`. A `fork()`
without `exec` costs almost nothing in C++.

The environment file has a format of our own (`NUL`-separated `KEY=VALUE`,
like `/proc/<pid>/environ`), as decided in
[libraries.md](libraries.md#3-operating-system-api-platform-layer).

### 5.2 Entry point

The child:

1. Sets up stdout and stderr as UTF-8 with backslash escapes for what cannot
   be encoded (`cc::out`, §12).
2. Sets `CC_LIB_DIR` and `CC_DATA_FILES_DIR` if they are not set.
3. Installs `SIGINT`/`SIGTERM` handlers that end the command with
   `128 + signum`.
4. Looks up `argv[1]` in the subcommand table (§5.3). Only that subcommand's
   parser is built. Without a subcommand, it prints the top-level help.
5. Runs the subcommand and returns its exit code.

Where the Python version prints a stack trace for an uncaught exception, the
port has internal errors instead (a failed check of an invariant, or a
standard library exception that nothing catches). A `std::set_terminate`
handler prints a stack trace on stdout and exits 1. `Interrupted by user...`
is printed for `SIGINT` as in the reference.

### 5.3 Subcommand table

A `constexpr` table in `src/launcher/` lists every subcommand:

```cpp
// One CodeChecker subcommand: its parser and its entry point.
struct Subcommand {
  std::string_view name;  // For example "analyze".
  std::string_view help;  // One-line help for the top-level list.
  void (*add_arguments)(argparse::Parser& parser);
  int (*run)(const argparse::Namespace& args);  // Returns the exit code.
};
```

| Library | Subcommands |
|---|---|
| `cc_common` | `version` |
| `cc_analyzer` | `analyze`, `analyzer-version`, `analyzers`, `check`, `checkers`, `fixit`, `log`, `parse` |
| `cc_web` | `web-version` |
| `cc_server` | `server` |
| `cc_client` | `cmd`, `store` |

Each subcommand lives in a `cli/` folder of its library, like the Python
`cli/` packages (`src/analyzer/cli/analyze.cc` ↔
`codechecker_analyzer/cli/analyze.py`).

### 5.4 Exit codes and errors

The port does not use exceptions (outside the Thrift boundary, see
[cppguide.md](cppguide.md#project-waivers) W1). The Python code calls
`sys.exit(n)` deep inside many functions; the port returns instead:

- A function that can fail returns `cc::Result<T>`: either the value, or an
  error with its message and the exit code the command should end with.
  Functions without a value to return use `cc::Result<void>`.
- Callers pass errors up until the subcommand's `run` function, which logs
  the message (if the reference logs one) and returns the exit code to the
  entry point.
- Because every exit is a normal return, destructors run and temporary files
  and folders are removed on the way out.
- `std::optional` is used where "no value" is not an error.

See also [libraries.md](libraries.md#errors-and-stack-traces).

## 6. Building blocks (`cc_platform`, `cc_util`)

These are described in [libraries.md](libraries.md) §1–3. In short:

- `cc_platform` is the only code that calls POSIX functions directly. It
  provides `Process` (spawn with an exact environment, own process group,
  pipes for stdout/stderr read at the same time, timeout that kills the group),
  signal handling through a self-pipe, `FileLock` (`flock`), temporary files
  and folders with Python's folder rules, `/proc` queries, and stack traces.
- `cc_util` holds the parts that must behave exactly like a Python module:
  `argparse`, `logging`, `json`, `yaml`, `plistlib`, `os.path` (`pypath`),
  `re` (`pyre`), `shlex`, `fnmatch`, `glob`, `prettytable` (`twodim`), and
  the Unicode helpers. Where the black-box suite cannot reach the edge
  cases easily, they also get unit tests (§13).

## 7. Report library (`cc_report`)

The report model is used by almost every command, so it is a library of its
own, as `codechecker_report_converter.report` is in Python
([report-format.md](../reference/report-format.md)).

```
src/report/
├── model/          Report, File, BugPathEvent, Range, ... ; path trimming
├── parser/         plist (libxml2 stream reader), SARIF (yyjson)
├── converter/      analyzer output → reports: sanitizers, clang-tidy text and
│                   YAML, gcc SARIF, cppcheck, infer, line formats, JSON/XML formats
├── output/         plist writer, JSON, SARIF, Code Climate, Gerrit, baseline,
│                   HTML (templates from lib/codechecker/html), text and tables
├── hash/           report hash algorithms
└── changed_files/  changed-file detection
```

| Used by | For |
|---|---|
| `analyze` | post-processing of analyzer plist files, failed-analysis reports |
| `parse`, `fixit` | reading result folders, all output formats |
| `report-converter` | `converter/` + `output/` |
| `store` (client) | reading the report folder, collecting source files |
| server task worker | parsing the uploaded plist files during a store |
| `cmd diff` | local and mixed diffs ([local-diff.md](../reference/local-diff.md)) |

## 8. Analyzer side (`cc_analyzer`, `cc_tu_collector`)

```
src/analyzer/
├── context/        AnalyzerContext: package layout, analyzer binaries,
│                   checker label database, environment (analyzer_context.py)
├── buildlog/       compilation database → build actions, uniqueing,
│                   compiler info detection (buildlog/, gcc_toolchain.py)
├── drivers/        one driver per analyzer: clangsa, clangtidy, cppcheck,
│                   gcc, infer (analyzers/)
├── analysis/       analysis manager, pre-analysis (CTU collect, statistics)
├── ctu/            CTU maps; also used by merge-clang-extdef-mappings
├── stats/          statistics post-processing; also used by post-process-stats
├── metadata/       metadata.json
└── cli/            analyze, check, log, parse, fixit, checkers, analyzers, analyzer-version
```

### 8.1 Analyzer drivers

Each analyzer is one class behind a common interface, as in
`analyzer_base.py` ([analyzer-drivers.md](../reference/analyzer-drivers.md)):

```cpp
// One analyzer (clangsa, clang-tidy, ...): how to query it, how to build its
// command line and how to turn its output into a result file.
class AnalyzerDriver {
 public:
  AnalyzerDriver(const AnalyzerDriver&) = delete;
  AnalyzerDriver& operator=(const AnalyzerDriver&) = delete;
  virtual ~AnalyzerDriver() = default;

  // Returns the analyzer's name, for example "clangsa".
  virtual std::string_view name() const = 0;
  // Runs the analyzer binary to find its version; empty if it cannot run.
  virtual std::optional<std::string> Version() const = 0;
  // Returns the checkers with their descriptions.
  virtual CheckerList Checkers() const = 0;
  virtual OptionList AnalyzerConfigOptions() const = 0;
  virtual std::vector<std::string> BuildCommand(
      const BuildAction& action, const AnalysisConfig& config) const = 0;
  virtual void PostAnalyze(const AnalysisResult& /*result*/) const {}
  // Returns the handler that turns the analyzer output into a plist file.
  virtual ResultHandler& result_handler() = 0;

 protected:
  AnalyzerDriver() = default;
};
```

The analyzer list is a fixed table, not discovered at run time. Results from
the analyzer binaries (version, checker lists, capabilities) are cached per
process, because `checkers` and `analyze` ask for them several times.

### 8.2 `analyze`

The main flow is a straight port of
[analyze.md](../reference/analyze.md) §2: validate options, gather and
parse the compilation database, pre-analysis, analysis, write side files and
`metadata.json`.

The analysis runs in a **thread pool** of `-j` threads. Each job starts one
analyzer process and waits for it (see
[libraries.md](libraries.md#threads-pools-and-timers)). Rules:

- Jobs are taken in job order. The progress counter (`[n/total]`) is an
  atomic counter, so numbers are unique and increasing, as in the reference.
- Results that the Python version collects in a `SyncManager` dict are plain
  structures behind one mutex.
- Log lines are written whole, so lines from different jobs never mix
  (§12).
- `--timeout`: the job waits with a deadline and kills the analyzer's whole
  process group when it passes.
- `SIGINT` stops handing out jobs, kills the running analyzer process groups
  and exits with the reference's code.
- Analyzers run in the original build environment
  (`CODECHECKER_ORIGINAL_BUILD_ENV`) plus the analyzer-specific settings.

CTU collect and statistics collection (pre-analysis) use the same pool.

### 8.3 `log`

`log` runs the build command through the shell in the original environment,
with `LD_PRELOAD` pointing at the copied `ldlogger.so` and the logger
variables from `config/config.json` ([log.md](../reference/log.md)). The
C++ port changes nothing in the logger itself.

### 8.4 `tu_collector`

`cc_tu_collector` is a library because `analyze` uses it for the
failed-analysis and reproducer ZIPs and for `--file` dependents
([tu-collector.md](../reference/tu-collector.md)); `bin/tu_collector` is a
small `main()` on top of it.

## 9. Thrift API (`cc_api`)

- **Code generation.** CMake runs the pinned `thrift` compiler with
  `--gen cpp` on every file in `api/` at build time and compiles the result
  into `cc_api`. Nothing generated is checked in.
- **API version.** CMake reads `api/api_version.json` and writes a header
  with the version (`6.74`), used for URLs on both sides. The same file is
  installed as `lib/codechecker/api_version.json`.
- **Protocol.** Messages are encoded with `TJSONProtocol` into a
  `TMemoryBuffer`, on both sides
  ([server-api.md](../reference/server-api.md) §1.1). Only the generated
  types, clients and processors are used from Thrift; Thrift's own HTTP
  transports and servers are not used.
- **Errors.** API handlers report errors by throwing the generated
  `RequestFailed`. Routing, authentication and unexpected errors become a
  `TApplicationException` of type `INTERNAL_ERROR` with the reference's
  messages (§1.2 there). This is the only place where the port uses
  exceptions ([cppguide.md](cppguide.md#project-waivers) W1): handlers call
  ordinary project code that returns `cc::Result<T>`, and convert its errors
  into `RequestFailed`. Code that only the handlers call and that must choose
  the error code itself returns `ApiResult<T>` (`server/api/request_failed.h`),
  whose `ApiError` carries the code.

Because the IDL files are unchanged, field ids, types and enum values are
the same as in the Python version and the web UI.

## 10. Client (`cc_client`)

```
src/client/
├── transport/      HttpTransport: a Thrift transport over libcurl
├── credentials/    session file, password file, token lookup, login
├── call/           call wrapper: Thrift errors → messages and exit codes
├── store/          store: ZIP assembly, blame info, upload, task polling
├── cmd/            one file per cmd subcommand
└── cli/            store, cmd (argument definitions)
```

- **Transport.** `HttpTransport` implements Thrift's `TTransport`: writes go
  into a buffer, `flush()` sends one HTTP `POST` with libcurl
  (`Content-Type: application/x-thrift`, `Authorization: Bearer <token>`),
  and reads come from the response. The generated client classes run on top
  of it unchanged. Proxy and CA variables are applied with Python's rules
  ([libraries.md](libraries.md#http-client-libcurl)).
- **Credentials.** `~/.codechecker.session.json` and
  `~/.codechecker.passwords.json` (or `CC_SESSION_FILE` / `CC_PASS_FILE`) are
  read and written under `flock`, so a Python client and a C++ client can share
  them ([server-api.md](../reference/server-api.md) §4.2).
- **Call wrapper.** Every API call goes through one function that turns
  `RequestFailed`, `TApplicationException` and connection errors into the
  reference's messages and exit codes (§5 there).
- **`store`.** Reads the report folder with `cc_report`, collects source
  files, runs `git` for blame info, builds the ZIP with libzip, compresses
  it with zlib, base64-encodes it, calls `massStoreRunAsynchronous` and
  polls the task until it is done ([store.md](../reference/store.md)).
- **`cmd`.** Each subcommand (`runs`, `results`, `sum`, `diff`, `history`,
  `suppress`, `products`, `components`, `permissions`, `token`, `login`,
  `export`, `import`, `filter-preset`, `serverside-tasks`, ...) is one file
  that builds its request, calls the API and prints with `twodim`.

## 11. Server (`cc_server`)

```
src/server/
├── supervisor/     main process: start-up, children, signals, instance list
├── http/           cpp-httplib setup, GET handling, POST routing, headers
├── api/            one handler class per Thrift service
├── database/       db::Connection, SQLite and PostgreSQL drivers, query
│                   builder, schema, products
├── auth/           sessions, tokens, login methods, permissions
├── tasks/          task manager, task queue, task worker, store task
├── gc/             garbage collection
└── cli/            server (argument definitions, --db-status, instance commands)
```

### 11.1 Processes

The process model is the reference's
([server.md](../reference/server.md) §6):

```
              ┌───────────────────────────┐
              │ supervisor (main process) │
              │ owns the listening socket │
              └──────┬─────────────┬──────┘
         fork() × N_api           fork() × N_task
                     │             │
  ┌──────────────────┴──┐      ┌───┴─────────────────┐
  │ API process         │ task │ task worker         │
  │ accept loop,        ├─────►│ runs background     │
  │ API handlers        │queue │ tasks (store)       │
  └──────────┬──────────┘      └──────────┬──────────┘
             └─── config DB, product DBs ─┘
```

- **Supervisor.** Does the start-up work (§11.2), binds the socket
  (`bind_to_port()`), forks the children and then only supervises: it
  restarts dead children (`SIGCHLD`), reloads the configuration (`SIGHUP`)
  and shuts down on `SIGINT`/`SIGTERM`, with the messages and exit codes of
  the reference. Signals reach its main loop through a self-pipe.
- **API processes.** Each calls `listen_after_bind()` on the inherited
  socket and serves requests. Like the reference, each one handles one
  request at a time (a cpp-httplib pool of one thread) and closes the
  connection after the response. Throughput comes from
  `--api-handler-processes`, as in the reference.
- **Task workers.** Each takes tasks from the queue and runs them
  ([cmd-serverside-tasks.md](../reference/cmd-serverside-tasks.md) §2).
- **Task queue.** A `socketpair(AF_UNIX, SOCK_SEQPACKET)` created by the
  supervisor before forking. API processes write to one end and task workers
  read from the other. Each message is one task (token and kind; the input
  data is in the task's folder under `<workspace>/codechecker_tasks/`). The
  socket keeps message boundaries, so several readers each get whole
  messages. For a synchronous store, the API process sends the write end of a
  "done" pipe along with the message (`SCM_RIGHTS`); the worker closes it
  when the task ends.

**Fork rules.** `fork()` is only called by the supervisor, and only while it
has no other threads (the parallel garbage collection threads at start-up are
joined first) and no open database connections. Children open their own
database connections. OpenSSL is initialized in the supervisor before
forking; the TLS context is shared, sessions are not.

### 11.2 Start-up

The order of [server.md](../reference/server.md) §4–5 and
[server-database.md](../reference/server-database.md) §3:

1. Configuration database check, `--db-status` and `--db-upgrade-schema`,
   the `Default` product, product status checks.
2. Garbage collection of the product databases (threads, joined), unless
   `--skip-db-cleanup`.
3. Drop the tasks left behind by an earlier server with the same machine id.
4. Read `server_config.json`, delete expired sessions, initialize permission
   records.
5. Bind the socket, set up TLS if `key.pem` and `cert.pem` exist.
6. Add the instance to `~/.codechecker.instances.json` (under `flock`).
7. Fork the API processes and task workers; print the listening address.
8. Wait for signals; on shutdown, stop the children, drop unfinished tasks,
   remove the instance record.

### 11.3 HTTP layer

The cpp-httplib configuration and the request rules of
[server-api.md](../reference/server-api.md) §2:

- `GET`/`HEAD`: `/live`, `/ready`, files from `<root>/www/` with the
  product prefix removed and the `index.html` fallback; directory listings
  → `405`.
- `OPTIONS`: always `403`.
- `POST`: parse the path (product, version, service), read the method name
  from the message, check the session, find the product, then call the
  service's processor.
- The security headers are added to every response; `X-User` and the cookie
  removal on `POST`.
- One INFO log line per call: `<host>:<port> -- [<user>] POST <path>@<method>`.

A `POST` is handled like this:

```
body ──► TMemoryBuffer ──► TJSONProtocol ──► <Service>Processor ──► handler
                                                  │                    │
response ◄── TMemoryBuffer ◄── TJSONProtocol ◄────┘◄───────────────────┘
```

A handler object is created for each request with a `RequestContext` (user,
groups, permissions, product and its database), the way the Python server
creates a handler per request. Handlers do not keep state between requests.

### 11.4 API handlers

| Service (URL name) | Handler | Python | Reference |
|---|---|---|---|
| `Authentication` | `AuthenticationHandler` | `api/authentication.py` | [server-authentication.md](../reference/server-authentication.md) |
| `Products` | `ProductHandler` | `api/product_server.py` | [cmd-products.md](../reference/cmd-products.md) |
| `Configuration` | `ConfigurationHandler` | `api/config_handler.py` | [server-api.md](../reference/server-api.md) §2.4 |
| `ServerInfo` | `ServerInfoHandler` | `api/server_info_handler.py` | [server-api.md](../reference/server-api.md) §2.4 |
| `Tasks` | `TaskHandler` | `api/tasks.py` | [cmd-serverside-tasks.md](../reference/cmd-serverside-tasks.md) |
| `CodeCheckerService` | `ReportHandler` | `api/report_server.py`, `api/mass_store_run.py` | the `cmd-*.md` pages, [store.md](../reference/store.md) |

`ReportHandler` is by far the largest part of the server. Its report filter
becomes one shared piece of code that turns a `ReportFilter` into SQL
through the query builder (§11.5).

### 11.5 Database layer

As decided in [libraries.md](libraries.md#database):

- `db::Connection` / `db::Statement` with one implementation for libsqlite3
  and one for libpq. Connection strings in the SQLAlchemy format stored in
  the `products` table are parsed into these
  ([server-database.md](../reference/server-database.md) §2).
- A query builder for the dynamic filter queries, which hides the
  SQLite/PostgreSQL differences.
- The schema of the configuration and the product database at the latest
  revision, as SQL per dialect, compiled into the program. The latest Alembic
  revision ids (`635389f535cd`, `feb5f01b5b52`) are written to
  `alembic_version` and used for the status checks. Upgrades from older
  revisions are not supported.
- Each API process keeps one connection per product it has used, opened on
  first use (products that failed to connect are retried as in the
  reference).

The details (interfaces, query builder, schema files) go into the database
design document.

### 11.6 Authentication

`auth/` ports `session_manager.py`, `permissions.py` and `auth/`:

- sessions and personal access tokens are rows in the configuration
  database, so every API process sees the same sessions;
- login methods: dictionary, LDAP (libldap), OAuth (libcurl and jwt-cpp),
  with regex groups and the super user rule. PAM is not ported: an enabled
  `method_pam` gets the reference's "prerequisites are NOT installed"
  warning and is turned off
  ([libraries.md](libraries.md#ldap-and-pam));
- the permission model and its checks, used by the handlers through
  `RequestContext`.

### 11.7 Background tasks

`tasks/` ports `task_executors/`: the task manager used by the API
processes (create the record, prepare the input folder, put the task on the
queue), the worker loop, and the store task body (the server side of
`store`, [store.md](../reference/store.md) §4). Cancellation is cooperative,
as in the reference: the task body checks at safe points whether it should
stop (cancel flag, or the worker received `SIGHUP` because the server is
shutting down).

### 11.8 Configuration reload

The reference reloads `server_config.json` only in the main process, so a
reload does not change request handling (⚠️ in
[server.md](../reference/server.md) §6). The port applies the reload where
requests are served: the supervisor checks the new file and then sends
`SIGHUP` to the API processes, which read it again between requests. Whether
this is accepted as a fix (with a `[fixed: known-issues #N]` test) is
decided with the known-issues list.

### 11.9 Instance list

`~/.codechecker.instances.json` is shared with Python servers. A record of
this host is alive if its process exists, belongs to the current user and
is a CodeChecker server: the reference checks that `argv[1]` ends with
`CodeChecker` or `codechecker_common/cli.py`; the port also accepts
`argv[0]` ending with `codechecker-cpp`, so each version recognizes the other's
servers ([server.md](../reference/server.md) §3).

## 12. Rules for matching Python behavior

Coding rules (naming, formatting, language features, error handling) are in
[cppguide.md](cppguide.md), the project's only coding guideline. This section
lists only what the code must do so that its results match the Python
version.

- **Namespaces per library.** Each library of §3 has its own namespace:
  `cc::platform`, `cc::util`, `cc::report`, `cc::common`, `cc::analyzer`,
  `cc::api`, `cc::web`, `cc::client`, `cc::server`.
- **Text.** All strings are UTF-8 in `std::string`. Text read from outside
  (files, process output) is decoded with the Python error handler the
  reference uses at that place (`ignore`, `replace`), so the rest of the code
  can rely on valid UTF-8.
- **Paths.** Paths are `std::string` in all interfaces and are handled with
  `pypath` (Python `os.path` rules). `std::filesystem` is used inside the
  platform layer and `pypath`, not in interfaces, so a path never changes by
  passing through a `std::filesystem::path`.
- **Output.** stdout and stderr are written only through `cc::out` (UTF-8
  with backslash escapes, Python's buffering and flush points) and the
  logger, never through `std::cout` directly.
- **Logging.** Named loggers (`system`, `analyzer`, `analyzer.clangsa`,
  ...) with the reference's levels and formats. Each line is written whole,
  so threads do not mix lines.
- **Order.** Where Python output follows `dict` insertion order, the code
  uses `OrderedMap`. `std::unordered_map` is only used where the order is
  never visible.
- **Sorting.** `std::ranges::stable_sort`, like Python's `sorted`.

## 13. Testing

CodeChecker's own tests (Python unit and functional tests, including
ldlogger's) are not migrated (see [README](README.md)). The port is checked
mainly by the black-box suite.

- **Black-box tests** (`blackbox_tests/`) are the main check. They run on the
  install tree: `CODECHECKER_BIN=<root>/bin/codechecker-cpp npm test`. A CMake
  target runs them after `install` into a temporary prefix. Each porting step
  (§14) is done when its part of the suite passes.
- **The suite is extended as needed.** When a ported part has behavior the
  suite does not cover yet, the test is added there, checked against the
  Python reference first, and then used for the port, as the suite's README
  describes.
- **Comparison mode** (`CODECHECKER_COMPARE_BIN`) runs the Python and the C++
  build side by side on the test projects.
- **Mixed tests.** Because the wire format is unchanged, the server tests can
  also run with the C++ client against a Python server and the other way
  round. This is how client and server can be ported one after the other.
  The suite starts servers with `CODECHECKER_BIN` today, so this is one of
  the extensions: an optional variable naming the build that runs
  `CodeChecker server` (for example `CODECHECKER_SERVER_BIN`). The suite
  starts the two builds separately, so neither one starts the other.
- **Unit tests** (GoogleTest) in `unit_tests/<library>/`, run with `ctest`,
  are a supporting tool for the Python-compatible building blocks (`pypath`,
  `shlex`, `fnmatch`, JSON and YAML writers, Unicode, `argparse`, report
  hash), where many small cases are easier to check directly. Expected values
  can come from running the Python function outside this repository; only
  the results are checked in, as data files.

## 14. Porting order

Each step ends with its part of the black-box suite passing.

| Step | Part | Why at this point |
|---|---|---|
| 1 | build system, `cc_platform`, `cc_util` (with unit tests where useful) | everything depends on it |
| 2 | `cc_report`, `report-converter`, `merge-clang-extdef-mappings`, `post-process-stats`, `tu_collector`, the small tools | standalone programs, easy to compare with the Python ones |
| 3 | launcher, `cc_common`, `version`, `analyzers`, `checkers`, `analyzer-version` | the `CodeChecker` program with read-only commands |
| 4 | `log`, `analyze`, `parse`, `check`, `fixit` | the local workflow; the largest part of the test suite |
| 5 | `cc_api`, `cc_web`, `cc_client`: `store`, `cmd` | tested against a **Python** server |
| 6 | `cc_server` | tested with the C++ client and the Python client, and with the copied web UI |

Within the `CodeChecker` program, subcommands that are not ported yet are
missing; the matching tests fail until they are. Unported parts are never
filled in by calling the Python version: Python never starts a C++ component
and C++ never starts a Python one (see [README](README.md)).

## Open questions

1. **Version numbers.** Decided 2026-10-25: `codechecker-cpp version`,
   `analyzer_version.json` and `web_version.json` report the reference's
   version, `6.30.0`, with the port's own git hash, git tag information and
   build date, computed the way `scripts/build/extend_version_file.py` does.
   `ServerInfo` and the store metadata send this value to the server, and the
   web UI shows it.
2. **Web UI build.** The web UI is built with `npm` from the copied sources.
   Does the CMake build run `npm ci && npm run build`, or does it take a
   prebuilt `www/` folder? Proposed in
   [build-system.md](build-system.md#45-web-ui): CMake builds it, and a
   prebuilt folder can be given instead.
3. **Threads per API process.** One thread, like the reference, is the safe
   start. More threads per process would need thread-safe handlers and one
   database connection per thread; decide after measuring.
4. **Configuration reload fix** (§11.8): accept it as a fix of the
   reference, or copy the reference behavior.
