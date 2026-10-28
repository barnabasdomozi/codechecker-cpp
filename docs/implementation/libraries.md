# Standard library vs. third-party libraries

Status: **accepted** (2026-10-25): both the third-party library choices and
the in-house parts. Nothing here is implemented yet. This page says which parts of the port can be written with
the C++ standard library (plus the operating system API), which parts we write
ourselves on top of it, and which third-party libraries we use.

Everything here was checked against the Python sources in `codechecker/`
(6.30.0, `451b0c5c`) and its virtual environment.

## Assumptions

- **Language level: C++20**, as the coding guideline
  [cppguide.md](cppguide.md) requires. The development machine has GCC 14.2
  and Clang 19.1, which support everything below (`std::format`,
  `std::jthread`, `std::source_location`, chrono time zones).
- **Platform: Linux only** (see [README](README.md)). The Python version
  also runs on macOS; the port does not. All OS calls still go into one
  platform layer (`src/platform/`), which may use Linux-only APIs (`/proc`,
  `signalfd`, `LD_PRELOAD`).
- **Matching behavior comes first.** The port must print the same output,
  write the same files and return the same exit codes as the Python version
  (see [README](../../README.md)). This is what most decisions below are
  based on.
- **Database: latest schema only.** The server creates and uses the current
  (latest) schema. Alembic migrations are not ported (see [Database](#database)).

## How we decide

1. **If the output must look exactly like a Python library's output, we
   write that part ourselves.** No C++ library prints `argparse` help,
   `prettytable` tables or `json.dumps` output the same way, and changing a
   third-party library to do so is more work than writing the code.
2. **If a Python dependency is a wrapper around a C library, we use that same
   C library.** It then behaves the same, error messages included. Examples:
   `lxml` → libxml2, PyYAML → libyaml, `python-ldap` → libldap,
   `sqlite3` → libsqlite3, `psycopg2` → libpq,
   `zlib` → zlib, `ssl` → OpenSSL. GitPython runs the `git` program, so we
   run it too.
3. **Protocols, file formats, compression and security code come from a
   library.** We do not write our own TLS, hashing, deflate, XML parser,
   JSON parser or Thrift serializer.
4. **Prefer libraries that are mature, packaged by the major Linux
   distributions and have a C or plain C++ API.** Every library is used only
   behind one internal interface.
5. **The analyzer must not depend on server libraries.** `analyze`, `parse`,
   `report-converter` and the other local tools must build and run without
   libcurl, cpp-httplib, Thrift (and the Boost headers it needs), the
   database drivers, jwt-cpp or LDAP.

## Summary

| Area | Python uses | C++ port | Kind |
|---|---|---|---|
| Strings, formatting | `str`, `%`, f-strings | `std::string`, `std::format` (+ small in-house `print`) | std |
| Containers, algorithms | `list`, `dict`, `set`, `sorted` | std containers, `<algorithm>`, `<ranges>` | std (+ one in-house ordered map) |
| File operations | `os`, `shutil`, `pathlib` | `std::filesystem` | std |
| Path string functions | `os.path.*` | in-house `pypath` on top of `std::filesystem` | in-house |
| Threads, pools, timers | `multiprocess.Pool`, `SyncManager`, `threading.Timer` | `std::jthread`, `std::mutex`, `std::condition_variable`, `std::counting_semaphore` | std |
| Date and time | `datetime`, `time` | `std::chrono` (with time zones) | std |
| Numbers ↔ text | `int()`, `float()`, `repr` | `std::from_chars`, `std::to_chars` | std |
| Command line parsing | `argparse` (+ `argcomplete`) | in-house `argparse` clone | in-house |
| Logging | `logging` | in-house | in-house |
| Tables | `prettytable` (+ `wcwidth`) | in-house | in-house |
| `shlex`, `fnmatch`, `glob`, `string.Template`, `html.escape`, `base64`, `semver` | stdlib / small packages | in-house | in-house |
| Unicode text handling | `str` | in-house UTF-8 helpers | in-house |
| Processes, signals, locks, temp files, `/proc` | `subprocess`, `signal`, `portalocker`, `tempfile`, `psutil`, `getpass` | POSIX API in the platform layer | OS API |
| Regular expressions | `re` | **PCRE2** | third-party |
| JSON | `json` | **yyjson** + in-house Python-compatible writer | third-party |
| YAML | PyYAML | **libyaml** + in-house type resolver and writer | third-party |
| XML, plist | `lxml`, `plistlib`, `xml.etree` | **libxml2** + in-house plist reader/writer | third-party |
| Compression | `zlib` | **zlib** | third-party |
| ZIP archives | `zipfile` | **libzip** | third-party |
| Hashes, UUIDs, random tokens, TLS | `hashlib`, `uuid.uuid4`, `os.urandom`, `ssl` | **OpenSSL 3** | third-party |
| HTTP client | Thrift `THttpClient`, `requests` | **libcurl** | third-party |
| HTTP server | `http.server` | **cpp-httplib** | third-party |
| RPC | `thrift` | **Apache Thrift** (C++ code generator + library), IDL files copied | third-party |
| Database drivers | SQLAlchemy over `sqlite3` / `psycopg2` / `pg8000` | **libsqlite3**, **libpq** | third-party |
| Database access layer | SQLAlchemy | in-house | in-house |
| Schema migrations | Alembic | not ported: latest schema only | — |
| JWT (OAuth ID tokens) | `authlib.jose` | **jwt-cpp** | third-party |
| OAuth 2 flow | `authlib` `OAuth2Session` | in-house on top of libcurl | in-house |
| LDAP | `python-ldap` | **libldap** (OpenLDAP) | third-party |
| PAM | `python-pam` | not ported: PAM authentication is never available | — |
| Git blame | GitPython | the `git` program | external tool |
| Unit tests | `pytest` | **GoogleTest** (development only) | third-party |

**The third-party choices in this table are fixed.** Replacing one of them, or
adding a new third-party library, needs a new decision recorded on this page.

The rest of this page explains each row.

## 1. Covered by the standard library

### Strings and formatting

`std::format` replaces f-strings and `str.format`. `std::print` is C++23,
so output goes through a small in-house `print` helper (`std::format` into a
buffer, then `fwrite`). The
format-spec syntax of `std::format` is modeled on Python's, so
`{:>10}`, `{:.2f}`, `{:08x}` and the like work the same.

Differences to keep in mind:

- `std::format("{}", 1.0)` prints `1`; Python's `str(1.0)` prints `1.0`.
  Floats printed "like Python" go through one helper (see
  [Numbers](#numbers--text)).
- Python `%`-formatting (`"%s: %d" % ...`, used by `logging`) has to be
  rewritten as `std::format` calls when porting. This is mechanical.

### Containers and algorithms

`std::vector`, `std::map`, `std::unordered_map`, `std::set`, `<algorithm>`
and `<ranges>` cover everything. Python's `sorted()` is stable, so it maps to
`std::ranges::stable_sort`, not `sort`.

The one missing piece is an **insertion-ordered map**. Python `dict` keeps
insertion order, and several outputs depend on it (JSON files, table rows,
checker lists). We add a small in-house `OrderedMap` (a vector of entries plus
a hash index) instead of pulling in a library for it.

### File operations

`std::filesystem` covers `os.makedirs`, `shutil.rmtree` (`remove_all`),
`shutil.copy*`, `os.listdir`/`os.scandir` (`directory_iterator`),
`os.stat`, `os.chmod`, `os.symlink`, `os.rename` and `os.path.exists`/`isdir`/`isfile`.

It does **not** cover the `os.path` string functions with the same results.
See [`pypath`](#ospath-functions-pypath).

### Threads, pools and timers

The Python version uses process pools (`multiprocess.Pool`) because of the
GIL. The actual work (running `clang`, `clang-tidy`, `cppcheck`, ...) always
happens in child processes, so in C++ a pool of threads that each start and
wait for a child process does the same job. `std::jthread`, `std::mutex`,
`std::condition_variable`, `std::counting_semaphore`, `std::latch` and
`std::stop_token` are enough to build it. `SyncManager` shared dicts become
plain structures behind a mutex. `threading.Timer` (analysis timeouts in
`analysis_manager.py`, `store.py`) becomes a thread waiting on a
condition variable with a timeout.

The server is different: it uses separate **processes** on purpose (one
supervisor, `N` API handler processes sharing the listening socket, `N` task
worker processes; see [server.md](../reference/server.md)). That is done with
`fork()` from the platform layer, and the task queue between processes needs
a pipe or Unix socket with our own message format. The standard library has
nothing for processes.

### Date and time

`std::chrono` covers `time.time()`, durations, `datetime.fromtimestamp` and
`strftime`-style formatting (`std::format("{:%Y-%m-%d %H:%M:%S}", tp)`).
GCC 14 supports the C++20 time zone database (`std::chrono::current_zone()`),
which replaces Python's local-time conversions. Python's `str(timedelta)`
format (`0:00:01.234567`) is a small in-house function.

### Numbers ↔ text

`std::from_chars` / `std::to_chars` give exact, locale-independent
conversions. `to_chars` without a precision produces the shortest text that
reads back to the same `double`, the same rule Python's `repr(float)` uses.
One in-house helper adds the remaining Python details (`1.0` instead of `1`,
`inf`/`nan` spelling).

### Errors and stack traces

The coding guideline does not allow exceptions outside the Thrift boundary
([cppguide.md](cppguide.md#project-waivers) W1). Functions that can fail
return a small in-house `Result<T>` type (the value, or an error with a
message and an exit code); `std::expected` would fit but is C++23. See
[architecture.md](architecture.md#54-exit-codes-and-errors) for how errors
reach the exit code.

The Python entry point prints a stack trace on stdout for uncaught exceptions
([common-infrastructure.md](../reference/common-infrastructure.md) §2). The
port prints one for internal errors, from a `std::set_terminate` handler.
`std::stacktrace` is C++23, so the platform layer uses glibc's `backtrace()`
and `backtrace_symbols_fd()` instead (function names need linking with
`-rdynamic`). The text will not look like a Python traceback, and it does not
need to.

### Random numbers

`std::mt19937` is enough where Python uses `random` for non-secret values
(random temporary file name suffixes in `tu_collector.py`, a random sample in
`product_server.py`). Session tokens, PKCE verifiers and OAuth `state` values
need secure random bytes (`os.urandom` in `session_manager.py`,
`authlib.generate_token`). The standard does not promise that
`std::random_device` is secure, so those use OpenSSL `RAND_bytes`.

## 2. Written in-house on top of the standard library

These parts must behave exactly like a Python module, and no existing C++
library does that. Each one is small enough to write and test.

### `argparse` clone

The command line interface must match Python `argparse` exactly: the help
layout and line wrapping (which depends on the `COLUMNS` variable), the
`usage:` line, error messages such as
`error: argument --checker-config: ...`, exit code 1 on parse errors,
mutually exclusive groups, `nargs` (`?`, `*`, `+`, `N`), `choices`,
"suppress" defaults where an option given vs. not given has to be told
apart, `allow_abbrev=False`, `--opt=value` and `-xVALUE`, and `--`.
[common-infrastructure.md](../reference/common-infrastructure.md) §4 lists the
details CodeChecker relies on, and the test suite checks the error texts.

CLI11, cxxopts and p-ranav/argparse were considered and **rejected**: each has
its own help format, its own error messages and its own parsing rules, and
none of them can be made to match `argparse` without changing them deeply.
Our clone only needs the `argparse` features CodeChecker uses, not all of
them.

Shell completion (`argcomplete`) is not part of the first version.

### Logging

The log line formats are fixed
(`[INFO 2026-10-25 09:57:50] - message`, and the longer `debug` format with
logger name, pid, thread id, file and line; see
[common-infrastructure.md](../reference/common-infrastructure.md) §6). We
also need the custom `DEBUG_ANALYZER` level and named loggers (`system`,
`analyzer`, `analyzer.clangsa`, ...). A small in-house logger using
`std::format` and `std::source_location` is less work than configuring
spdlog to produce the same output, so spdlog is **not** used.

### Text tables (`prettytable` + `wcwidth`)

`twodim.py` prints tables with `prettytable` in the `SINGLE_BORDER` style
(box-drawing characters), plus `rows`, `csv`, `json` and `dictlist` formats.
It also has its own algorithm to fit the table to the terminal width
(`_fit_table_to_width`). `prettytable` measures text with `wcwidth`, so
CJK characters count as two columns and combining characters as zero. We
write the table code ourselves and generate the character-width table from
the same Unicode data that the `wcwidth` version in the reference
environment (0.9.3) uses, so column widths match.

### Small Python modules

| Python | Used for | Notes |
|---|---|---|
| `shlex.split`, `quote`, `join` | compile commands, build commands, analyzer command lines (34 call sites) | Must follow POSIX-mode `shlex` rules exactly; the `odd-paths` test project (spaces, apostrophes, non-ASCII names) tests this. |
| `fnmatch.fnmatch`, `fnmatch.translate` | skip files, review status rules, `parse --file`, tu_collector | Python's `*` also matches `/`, and there is no backslash escape. POSIX `fnmatch(3)` is close but not the same. The code calls `translate()` and joins the results into one regex, so we port `translate()` and run the result with PCRE2. |
| `glob.glob` | finding plist files, logger libraries, cppcheck results | All uses are simple patterns or `**`. Done with `directory_iterator` + our `fnmatch`. Python's `glob` does not sort its results, and the port should not sort either. |
| `string.Template` | HTML report templates | `$name` / `${name}` substitution. |
| `html.escape` | HTML export, server | Five replacements. |
| `base64` | store, server, `convert.py` | Python's `b64decode` ignores characters outside the alphabet by default; ours must too. |
| `semver.Version` | analyzer version checks | Parse and compare `major.minor.patch[-pre][+build]`. |
| `uuid.uuid4` | temporary names | 16 bytes from OpenSSL `RAND_bytes`, version and variant bits set, formatted as a UUID. |
| `os.path.expandvars` | `--config` option values | `$VAR` and `${VAR}`; unknown names stay as they are. |

### Unicode text handling

C++ strings are bytes; Python strings are code points. This matters in
several places, and it must be handled in one shared module:

- **Decoding with `errors='ignore'`** (90+ call sites), `'replace'` and
  `'backslashreplace'` (stdout/stderr). We need a UTF-8 decoder with the
  same error handlers. Once decoded, the rest of the program can rely on the
  text being valid UTF-8.
- **The report hash** ([report-hash.md](../reference/report-hash.md)) slices
  the source line by **code point** index (`line_content[:old_col]`) and
  removes whitespace with `str.split()`, which splits on all Unicode
  whitespace, not just ASCII (`"a\x1cb\xa0c\u2003d".split()` gives four
  parts). Hashes are stored on the server and compared between runs, so the
  C++ version must produce exactly the same hash. We need code-point indexing
  and Python's whitespace table.
- **Universal newlines.** Python text mode turns `\r\n` and a lone `\r` into
  `\n`, so line numbers count a lone `\r` as a line break. `get_line()` and
  the source code comment parser depend on this.
- `str.lower()` / `str.upper()` on non-ASCII text. In practice they are
  applied to checker names and options (ASCII), so ASCII-only versions are
  enough. A `debug` assertion catches non-ASCII input.

No library is needed: the decoder, the whitespace table and code-point
iteration add up to a few hundred lines.

### `os.path` functions (`pypath`)

`std::filesystem` has similar functions, but they give different results.
Checked on the development machine:

| Input | Python | `std::filesystem` |
|---|---|---|
| `normpath("a/b/")` | `a/b` | `lexically_normal()` → `a/b/` |
| `normpath("")` | `.` | `lexically_normal()` → `` (empty) |
| `normpath("//a/b")` | `//a/b` (POSIX keeps exactly two leading slashes) | `lexically_normal()` → `/a/b` |
| `realpath()` of a missing path | resolves what exists, never fails | `canonical()` throws; `weakly_canonical()` is close but handles `..` after missing parts differently |
| `abspath(p)` | `normpath(join(cwd, p))`, does not look at the disk | `absolute()` does not normalize |
| `relpath`, `commonpath`, `expanduser` | — | no exact equivalent |

Paths are written to plist files, JSON, the database and report hashes, so we
write a small `pypath` module with Python's `posixpath` semantics on top of
`std::filesystem` and test it against the cases in CPython's
`test_posixpath`.

### JSON writer

See [JSON](#json): we parse with a library but write Python-style JSON with
our own code.

### YAML type resolver and writer

See [YAML](#yaml).

### plist reader and writer

See [XML and plist](#xml-and-plist).

### Database access layer

See [Database](#database).

### OAuth 2 flow

`authlib`'s `OAuth2Session` is used for: building the authorization URL with
`state` and a PKCE (S256) challenge, exchanging the code for a token (form
POST), and calling the user-info, groups and e-mail endpoints with a bearer
token. That is a few HTTP requests with libcurl and JSON. No C++ OAuth client
library is worth adding for it. ID-token signature checks use
[jwt-cpp](#jwt).

## 3. Operating system API (platform layer)

These are not in the C++ standard library, but they are not third-party
libraries either: they are POSIX/Linux system calls, wrapped in our
platform layer (`src/platform/`).

| Python | Used for | Linux API |
|---|---|---|
| `subprocess` | running analyzers, compilers, build commands (`log -b`), `git` | `posix_spawn` / `fork` + `exec`, pipes, `poll`, `waitpid`, process groups |
| `signal` | forwarding `SIGINT`/`SIGTERM`/`SIGHUP`/`SIGCHLD`, exit with `128 + signum` | `sigaction`, self-pipe or `signalfd` |
| `psutil` | killing a process tree (`process.py`), checking a server pid and its owner (`instance_manager.py`) | read `/proc/<pid>/stat`, `/proc/<pid>/status` |
| `portalocker` | locking `~/.codechecker.passwords.json`, the server instance list, and JSON files while they are read (`util.load_json`) | `flock(2)` (what `portalocker` uses on Linux) |
| `tempfile` | temporary files and folders | `mkstemp`, `mkdtemp`; the temp folder is chosen like Python's `gettempdir()` (`TMPDIR`, `TEMP`, `TMP`, then `/tmp`, `/var/tmp`, current folder) |
| `getpass` | password prompt, current user name | `termios` (turn off echo), `getpwuid` |
| `socket` options | keep-alive settings on the server socket | `setsockopt` |
| `os.environ` | reading and changing the environment | `environ`, `setenv`, `unsetenv` |
| `platform` | OS checks | `uname` |

**Processes: in-house, not a library.** We write our own thin wrapper because
the port needs control that process libraries hide or make awkward: separate process groups so
a timeout kills the whole analyzer tree, an exact environment for the child
(the original build environment and `LD_PRELOAD` for ldlogger; see
[log.md](../reference/log.md)), reading stdout and stderr at the same time
without blocking, and running build commands through the shell. The wrapper
is a few hundred lines and the black-box suite tests it heavily.

**The original-environment file.** The Python launcher saves the environment
with `pickle` (`CODECHECKER_ORIGINAL_BUILD_ENV`). The Python launcher never
starts a C++ component (see [README](README.md)), so the C++ side never reads
a pickle. The file format is ours: `NUL`-separated `KEY=VALUE`, like
`/proc/<pid>/environ`.

## 4. Third-party libraries

### Regular expressions: PCRE2

**Needed for:** about 210 `re` calls (139 compiled patterns), most of them in the report-converter
parsers for analyzer output (sanitizers, clang-tidy, GCC, cppcheck, ...), in
`log_parser.py` (24 patterns), in the server, and in **user-supplied**
patterns (`--compile-uniqueing`, checker name regexes).

**Why not `std::regex`:**
- It has no named groups (`(?P<name>...)`, used in 26 files) and no
  lookbehind (`(?<!\\):` in `cmd_line_client.py`,
  `(?<!...)` in `session_manager.py`). Both fail to compile with
  `std::regex`.
- libstdc++'s `std::regex` uses recursion. On the development machine,
  `regex_match` of `(a|b)*` against a 200 000-character string **crashed
  the process** (stack overflow, signal 11). Build logs and analyzer outputs
  are this long.
- It is known to be slow.

**Why not RE2:** no lookbehind and no backreferences, so it cannot run every
pattern a user could pass in Python syntax.

**PCRE2** supports Python's syntax, including `(?P<name>...)`,
lookaround, non-greedy matches and inline flags, and it has a JIT compiler. To
get Python's results:
- `re.match` → `PCRE2_ANCHORED`; `re.fullmatch` → also
  `PCRE2_ENDANCHORED`.
- Python's `\Z` means "end of string", which is `\z` in PCRE2; patterns are
  translated when compiled.
- Python `str` patterns are Unicode by default (`\w`, `\d`, `\s`), so we
  compile with `PCRE2_UTF | PCRE2_UCP`.
- `re.sub` replacement strings (`\1`, `\g<name>`) are translated to PCRE2's
  substitution syntax.

All of this goes into one `pyre` wrapper.

### JSON

**Needed for:** compilation databases, `metadata.json`, checker label files,
SARIF input and output, `parse --export json`, `cmd ... -o json`, config
files, the session file, the server instance list.

**Choice: yyjson** (C library, MIT license). It is one `.c`/`.h` pair, among
the fastest JSON readers and writers, and has no dependencies, so the analyzer
side can use it. Objects keep their members in document order, like a Python
`dict` built by `json.loads`. Read flags we use (checked against yyjson
0.13.0):
- `YYJSON_READ_ALLOW_INF_AND_NAN`: Python's `json` reads and writes `NaN`,
  `Infinity` and `-Infinity` by default.
- `YYJSON_READ_BIGNUM_AS_RAW`: Python integers have no size limit; numbers
  that do not fit 64 bits are kept as text instead of being turned into a
  `double`.

Two differences from Python are handled where documents are converted:
- **Duplicate keys.** yyjson keeps all of them and `yyjson_obj_get` returns
  the first; `json.loads` keeps the **last** value.
- **Lookups are linear** (`yyjson_obj_get` scans the object). Large objects
  such as label files (thousands of checker keys) are converted into our own
  maps right after parsing, not queried in place.

**The writer for Python-compatible output is ours.** Python's `json.dumps`
defaults differ from yyjson's writer:
- separators `", "` and `": "` even without `indent`
  (`{"a": 1, "b": 2}`); yyjson writes `{"a":1,"b":2}`;
- `ensure_ascii=True`: non-ASCII becomes `\uXXXX`, characters outside
  the BMP become surrogate pairs (`"😀"`);
- `indent=2` / `indent=4` layouts, `sort_keys=True`.

The test suite compares JSON as data, so most files only need to be valid and
equal. But some hashes are computed from JSON text, for example
`CheckerSet.compute_hash` in `run_db_model.py` hashes
`json.dumps(..., sort_keys=True, separators=(',', ':'))`. That text has to
be identical byte for byte, or the stored hashes will not match. Our writer
walks yyjson documents (immutable or mutable) and is about 150 lines.

Thrift messages do not go through yyjson: `TJSONProtocol` has its own JSON
code.

### YAML

**Needed for:** `--config` files (read with `BaseLoader`: every value is a
string), `review_status.yaml` (`safe_load`), clang-tidy config dumps, fixit
YAML files (read and **written**, `fixit.py` also prints `yaml.dump()` to
stdout), CTU invocation lists, statistics output.

**Choice: libyaml** (the C library). PyYAML is a YAML **1.1** parser, and its
C extension is built on libyaml, so libyaml accepts the same documents and
reports errors the same way. yaml-cpp and rapidyaml follow YAML 1.2, which
types plain values differently.

PyYAML's `safe_load` applies YAML 1.1 type rules. Checked in the reference
environment:

```
a: yes   → True        b: 0777  → 511
c: 1:30  → 90          d: off   → False
```

libyaml only produces events and plain strings, so we write the type
resolver ourselves, copying the regular expressions in PyYAML's
`resolver.py`. `BaseLoader` mode is the same code without the resolver.

`yaml.dump` output must also match: keys sorted, block style, PyYAML's quoting
rules (`'on'`, `'x: y'` are quoted) and line folding:

```
yaml.dump({"z": "x: y", "a": [1, "on"], "m": "multi\nline"})
→ "a:\n- 1\n- 'on'\nm: 'multi\n\n  line'\nz: 'x: y'\n"
```

We use libyaml's emitter (PyYAML's C dumper uses the same one) and write the
part that picks quoting style and sorts keys ourselves.

### XML and plist

**Needed for:** reading Clang SA `.plist` files (the main analyzer output,
sometimes very large), writing plist files (`plistlib.dump` after
post-processing), and reading cppcheck, SpotBugs and Roslynator XML.

**Choice: libxml2.** `lxml` is a binding of libxml2, and `plist.py` parses
plists with `lxml` for speed. With libxml2 we accept and reject the same files,
and the `Invalid plist file '<file>': <message>` log line can carry the
same message. libxml2's streaming API (`xmlTextReader` or SAX) handles large
files without loading a full DOM. We keep lxml's default size limits
(`XML_PARSE_HUGE` off) so huge-file behavior matches. libxml2 is already
installed on the development machine.

pugixml (faster, DOM only) and Expat were considered; they would accept and
reject different inputs than `lxml` does.

The plist **data model** (`dict`, `array`, `string`, `integer`, `real`,
`true`/`false`, `date`, `data`) and the **writer** are in-house. The
writer must produce `plistlib.dump`'s exact layout: XML header, the Apple
`DOCTYPE`, tab indentation and its escaping rules.

### Compression: zlib

**Needed for:** `store` compresses the report ZIP with
`zlib.compress(..., Z_BEST_COMPRESSION)` before base64-encoding it; the server
decompresses it with a size limit (`decompressobj().decompress(...,
max_length=...)`, used to reject ZIP bombs) and stores source files, bug
paths, blame info, analyzer versions and commands **zlib-compressed in the
database**.

**Choice: zlib.** The data is in
the zlib format, not raw deflate or gzip, and databases written by the Python
server must stay readable, so the format is fixed. The size-limited
decompression maps to `inflate()` with a bounded output buffer.

### ZIP archives: libzip

**Needed for:** the `store` upload archive (ZIP64 enabled), the server
unpacking it, `tu_collector` and failed-analysis ZIPs (opened in append mode
`'a'`), and `memory_safety_reporter`.

**Choice: libzip.** It supports reading, writing, appending to existing
archives, deflate and ZIP64.

**No duplicate names.** Python's `zipfile` in append mode allows two
entries with the same name (it only warns: `UserWarning: Duplicate name:
'x'`, and `namelist()` then lists both). The port does not copy this: an
archive it writes never contains two entries with the same name. Adding a
name that already exists replaces the earlier entry (`ZIP_FL_OVERWRITE`),
which matches what a reader of the Python archive gets, since `zipfile`
returns the last entry of a name.

### Hashes, UUIDs, secure random bytes, TLS: OpenSSL 3

**Needed for:**
- MD5: report hashes, build-action hashes, `makefile` target names, report
  folder names in `store`.
- SHA-256: file content hashes in `store`/server, `CheckerSet` hashes,
  `memory_safety_reporter` checksums, random token strings
  (`codechecker_common/util.py`).
- Any `hashlib` algorithm chosen by name: password hashes for the
  dictionary authentication in `server_config.json`
  (`getattr(hashlib, hash_algorithm)` in `session_manager.py`). OpenSSL's
  `EVP_get_digestbyname` covers these, but some names differ (Python
  `sha3_256` is OpenSSL `SHA3-256`), so we need a name table.
- Secure random bytes (`RAND_bytes`): session tokens, OAuth `state` and PKCE
  verifier, and the bytes of `uuid4` UUIDs (formatting is ours).
- TLS for the server (`cert.pem` / `key.pem`, TLS 1.0 and 1.1 disabled; see
  [server.md](../reference/server.md)) through cpp-httplib, and for the client
  through libcurl.

Python's `hashlib` and `ssl` use OpenSSL, so this is the same code. One
consequence: the analyzer side also links `libcrypto` for MD5. That is
acceptable; libcrypto is present on every Linux system.

### HTTP client: libcurl

**Needed for:** every client command talking to the server (`store`, `cmd`,
`login`, ...: Thrift calls over HTTP POST), and server-side OAuth requests.

**Choice: libcurl.** Thrift's own C++ `THttpClient` does not handle proxies
and has limited TLS options. libcurl handles proxies, `CONNECT` and TLS, but
its defaults differ from Python's in two documented ways
([server-api.md](../reference/server-api.md) §3.5), so we set these
ourselves:
- **Proxy:** Python reads `http_proxy`/`HTTP_PROXY`, `https_proxy`/
  `HTTPS_PROXY` and `no_proxy`. libcurl ignores upper-case `HTTP_PROXY`.
  We read the variables with Python's rules, check the format (the
  `Invalid proxy format! ...` error, exit 1) and pass the result with
  `CURLOPT_PROXY` (`""` when no proxy is used, so libcurl does not pick one
  from the environment itself). `no_proxy` is matched with Python's rules
  (`proxy_bypass_environment()`) as well, not with `CURLOPT_NOPROXY`:
  libcurl also matches IP ranges in CIDR notation (`10.0.0.0/8`), which
  Python does not, so such an entry would bypass the proxy only in the
  port.
- **Trusted certificates:** Python honors `SSL_CERT_FILE`/`SSL_CERT_DIR`.
  libcurl uses the CA bundle chosen when it was built. We pass the variables
  with `CURLOPT_CAINFO` / `CURLOPT_CAPATH` when they are set.

### HTTP server: cpp-httplib

**Needed for:** serving the web UI files (`www/`), the Thrift `POST`
endpoints per product and API version, cookies, the `OPTIONS` → 403 rule, security headers, TLS, IPv4 and
IPv6 (see [server-api.md](../reference/server-api.md)), and the pre-fork
model: several worker processes accepting on **one** listening socket that
the main process created.

**Choice: [cpp-httplib](https://github.com/yhirose/cpp-httplib)**
(header-only, MIT license, TLS through OpenSSL with
`CPPHTTPLIB_OPENSSL_SUPPORT`). How the needs map to its API (checked against
0.63.0; we pin one version and check again when updating):
- **Pre-fork model.** cpp-httplib creates its own listening socket, so the
  main process calls `bind_to_port()` **before** `fork()`, and each API
  worker calls `listen_after_bind()` on the inherited socket. The thread
  pool is created inside `listen_after_bind()` (`new_task_queue`), so no
  threads exist at fork time. `new_task_queue` also sets the number of
  threads per worker.
- **TLS:** `SSLServer` with a context setup callback, which loads
  `cert.pem` / `key.pem` and sets the minimum protocol version so TLS 1.0
  and 1.1 stay disabled.
- **IPv4 / IPv6:** `set_address_family()`; keep-alive and other socket
  options through `set_socket_options()`. Its default options set
  `SO_REUSEPORT`, which would let a second server listen on a port that is
  in use, so the server replaces them with Python's `SO_REUSEADDR`.
- **Security headers:** `set_default_headers()` (or
  `set_post_routing_handler()` where a header depends on the request).
- **`OPTIONS` → 403:** an `Options()` handler for every path.
- **Static files:** `set_mount_point()` for `www/`, or our own handler where
  the Python server's rules for file paths differ.
- **Upload size:** the default request body limit is 100 MB
  (`CPPHTTPLIB_PAYLOAD_MAX_LENGTH`); we set `set_payload_max_length()`
  explicitly so large `store` uploads are not cut off.

cpp-httplib uses blocking sockets with one pool thread per connection, which
fits the server's model of several processes with a few threads each.

### RPC: Apache Thrift

**Needed for:** the `codechecker_api` interfaces (`report_server.thrift`,
`products.thrift`, `authentication.thrift`, `configuration.thrift`,
`server_info.thrift`, `tasks.thrift`, `shared.thrift`), the same on the
server and the client. The protocol must stay compatible with the web UI,
which is copied unchanged and calls the server through its generated
JavaScript Thrift client (see [README](README.md)).

**Choice: Apache Thrift** C++ code generator and library.
- **The IDL files are copied** from `codechecker_api/` unchanged, together
  with `api_version.json`. The C++ code is generated from them at build time
  by CMake running the `thrift` compiler. Nothing generated is checked in.
- Like the Python server, we only use the generated types and processors plus
  `TJSONProtocol` over `TMemoryBuffer`. The HTTP transport is ours:
  cpp-httplib on the server, libcurl on the client.
- We pin one Thrift version from the range the Python side allows
  (`thrift~=0.22`; the web UI uses `thrift ^0.22.0`, and 0.25.0 is installed
  in the reference environment).

Because the wire format is the same, Python clients can talk to the C++
server and the C++ client can talk to a Python server.

**Boost headers.** libthrift needs Boost headers to build (checked in 0.22.0:
`lib/cpp/CMakeLists.txt` calls `REQUIRE_BOOST_HEADERS()`), and some of its
public headers include Boost (`TTransportException.h` includes
`boost/numeric/conversion/cast.hpp`). So everything that includes the
generated code also needs Boost headers at build time. No Boost library is
linked, and our own code does not use Boost.

### Database

**Needed for:** two kinds of databases (the configuration database and one
database per product), each either SQLite or PostgreSQL, with about 130
queries in `report_server.py` alone, many of them built dynamically from
report filters (joins, subqueries, `GROUP BY`, `CASE`, paging).

**Drivers: libsqlite3 and libpq.** These are the C libraries under Python's
`sqlite3` and `psycopg2`. `pg8000` is pure Python, so for it there is no C
library to match; libpq replaces it.

**Access layer and query building: in-house.** A thin
`db::Connection` / `db::Statement` interface with one implementation per
driver, and a small query builder for the dynamic filter queries that handles
the SQLite/PostgreSQL differences SQLAlchemy hides today (placeholders,
`RETURNING`, boolean and `BIGINT` types, `ILIKE`/`LIKE ... ESCAPE`,
`PRAGMA foreign_keys=ON` on SQLite). The details get their own database
design document.

**Schema: latest only, no migrations.** The Alembic migrations (51 between
the two databases) are not ported. The C++ server creates new databases
directly with the current (latest) schema of each database, written as SQL
per dialect. It only supports databases that already have that schema; older
databases have to be upgraded with the Python version first. The schema
version shown by `server --db-status` is the Alembic revision id in the
`alembic_version` table ([server-database.md](../usage/server-database.md)),
so the C++ server still writes the latest revision id there and uses it to
recognize a database with the current schema.

### JWT

**Needed for:** checking the signature of OAuth ID tokens (RS256) against the
provider's JWKS (`authlib.jose.JsonWebToken` in `authentication.py`).

**Choice: jwt-cpp** (header-only, uses OpenSSL). It supports RS256 and JWKS.
Writing JWT checks by hand is security code we should not own.

jwt-cpp parses JSON through a "traits" type. We use its default traits
(picojson, bundled with jwt-cpp) and do not write yyjson traits. picojson is
only used inside jwt-cpp, for ID tokens and JWKS; it is not a separate
dependency.

### LDAP and PAM

**libldap** (OpenLDAP) for LDAP authentication; `python-ldap` is a binding
of it. `ldap.filter.escape_filter_chars` is a few lines we write ourselves.

**PAM is not ported** (decided 2026-10-25). There is no libpam
dependency. The port behaves like the Python server when `python-pam` is
not installed, which is also the case in the reference environment:
`session_manager.py` then lists `pam` in `UNSUPPORTED_METHODS`. A
`server_config.json` with `method_pam.enabled` gets the WARNING
`PAM authentication was enabled but prerequisites are NOT installed on the
system... Disabling PAM authentication.` at start-up
([server-authentication.md](../reference/server-authentication.md) §1),
and PAM is then turned off.

### Git blame: the `git` program

**Needed for:** `store` collects blame information for source files
(`blame_info.py`): repository root, ignored files, tracking branch, remote
URL, and per-line commit, author, summary, message and date.

**Choice: run `git`.** GitPython does not implement Git itself; it runs
`git blame -p --incremental` and `git cat-file` and parses the output. Doing
the same gives the same results, including rename detection, `.mailmap` and
user Git config. libgit2 was **rejected**: its blame does not always agree
with `git`'s, and the stored blame info would change.

### Unit tests: GoogleTest

The port is checked mainly by the black-box suite in `blackbox_tests/` (Jest),
which runs the finished programs; CodeChecker's own `pytest` tests are not
migrated (see [README](README.md)). Unit tests are an addition, for in-house
modules where many small cases are easier to check directly than through a
command (`pypath`, `shlex`, `fnmatch`, the JSON and YAML writers, Unicode
helpers, `argparse`). They use **GoogleTest**, which is only needed for
development builds. Many expected values can come from running the
Python function on a list of inputs. Only the results are checked in, as data
files; the repository contains no Python code (see [README](README.md)).

## 5. Dependencies by component

| Component | Third-party libraries |
|---|---|
| Common (CLI, logging, paths, Unicode, processes) | PCRE2, yyjson, libyaml |
| `analyze`, `check`, `parse`, `fixit`, `log`, `checkers`, `analyzers` | + libxml2, zlib, libzip, OpenSSL (libcrypto) |
| `report-converter`, `tu_collector`, other local tools | + libxml2, zlib, libzip, OpenSSL (libcrypto) |
| `store`, `cmd` (client) | + libcurl, Apache Thrift (+ Boost headers), OpenSSL (libssl) |
| `server` | + cpp-httplib, Apache Thrift (+ Boost headers), libcurl (OAuth), libsqlite3, libpq, jwt-cpp, libldap |
| development only | GoogleTest |
| run time, not linked | `git` (blame), the analyzers themselves |

`ldlogger` (C) and the web UI are copied unchanged (see [README](README.md)).

The build system is CMake. Every library above is a git submodule under
`third_party/`, compiled from source and linked statically; the pinned
versions and build settings are in [build-system.md](build-system.md).

## Open questions

None. The earlier questions (platform scope, Python launcher starting C++
components, ZIP duplicate names, JSON for jwt-cpp) are decided above and in
[README](README.md).
