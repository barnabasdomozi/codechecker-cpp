# Reference: `CodeChecker log` and `ldlogger`

Usage: [../usage/log.md](../usage/log.md)

Python source: `analyzer/codechecker_analyzer/cli/log.py`,
`buildlog/build_manager.py`, `buildlog/host_check.py`, `env.py`.
C source: `analyzer/tools/build-logger/src/` (`ldlogger-hooks.c`,
`ldlogger-logger.c`, `ldlogger-tool.c`, `ldlogger-tool-gcc.c`,
`ldlogger-tool-javac.c`, `ldlogger-util.c`).

## 1. Command

```
CodeChecker log -b COMMAND -o LOGFILE [-k] [-q] [--use-absolute-ldpreload-path] [--verbose L]
```

Steps:

1. `LOGFILE` is made absolute and canonical (symlinks resolved).
2. `LOGFILE` is **overwritten** with the 3 bytes `[\n]` (a valid empty JSON
   array), so the file is valid even if nothing gets logged.
3. Log `Starting build...`.
4. Load the **original** environment (the one saved by the launcher, see
   [common-infrastructure.md](common-infrastructure.md) §2). If loading fails,
   warn and use a copy of the current environment.
5. Choose a logging method:
   - **ldlogger** if `ldlogger.so` exists under
     `$CC_DATA_FILES_DIR/ld_logger/lib/**/` (or the alternative library path)
     and the OS is Linux. Log `Using CodeChecker ld-logger.`
   - else **intercept-build** if `intercept-build --help` runs with exit 0.
     Log `Using intercept-build.`; the command becomes
     `intercept-build --cdb <LOGFILE> sh -c <shell-quoted COMMAND>`, run in
     the original environment.
   - else error and exit 1, with a platform-specific message (Linux: "Both
     ldlogger and intercept-build are unavailable. ..."; Windows: suggests
     CMake / compiledb; macOS: "Intercept-build is required to run
     CodeChecker in OS X.").
6. Build the logger environment (ldlogger method), starting from the
   original environment:
   - `CC_LOGGER_BIN=<root>/bin/ld_logger` (legacy, unused by the library).
   - `LD_PRELOAD`:
     - default: append `ldlogger.so` (just the name) to `LD_PRELOAD`
       (space-separated) and **prepend** all architecture folders of
       `ld_logger/lib/` (e.g. `.../lib/32bit:.../lib/64bit`) to
       `LD_LIBRARY_PATH`. The dynamic loader picks the folder that matches the
       process architecture.
     - with `--use-absolute-ldpreload-path`: the full path of the library in
       the folder matching the Python process architecture (`64bit` or
       `32bit`); if no folder matches, warn and use the first folder.
       `LD_LIBRARY_PATH` is not changed.
   - `CC_LOGGER_FILE=<LOGFILE>`.
   - `CC_LOGGER_GCC_LIKE` defaults to `gcc:g++:clang:clang++:/cc:c++` if not
     set by the user.
   - `CC_LOGGER_KEEP_LINK=true` if `-k` is given or the user already set it
     to `true`.
   - Debug file: if `--verbose` is `debug`/`debug_analyzer`, set
     `CC_LOGGER_DEBUG_FILE` to the user's value, or to
     `<dir of LOGFILE>/codechecker.logger.debug`; an existing file at that
     path is deleted first. Otherwise `CC_LOGGER_DEBUG_FILE` is removed from
     the environment.
7. Run `COMMAND` through the shell (`sh -c`), with stderr merged into stdout.
   Each output line is echoed unless `-q`.
8. Exit status 0 → `Build finished successfully.` (and exit 0). Non-zero →
   `Build failed.` and **exit with the build's exit code**. Exception when
   starting the command → `Calling original build command failed.`, exit 1.
9. Always: if a debug file was set, log `The debug log file is: <path>` and
   delete `<debug file>.lock`; delete `<LOGFILE>.lock`.

## 2. The preload library (`ldlogger.so`)

### Interception

The library defines these functions; each logs the call, then calls the real
function found with `dlsym(RTLD_NEXT, name)` (if that lookup fails, it returns
-1):

`execv`, `execve`, `execvp`, `execvpe` (GNU), `posix_spawn`, `posix_spawnp`.

For `exec*`: before calling the real function, if the program path is exactly
`ldd` or ends with `/ldd`, `LD_PRELOAD` is unset.

The logged argument vector is `[path_or_file, argv[0], argv[1], ...]`. The
logger entry point receives `prog = path_or_file` and `args = argv`.

### Per-call processing (`logExec`)

1. Fewer than 2 entries → ignore.
2. `CC_LOGGER_FILE` unset → error, ignore.
3. Take an exclusive `flock` on `<abs CC_LOGGER_FILE>.lock` (created if
   needed, mode 0600). This serializes parallel compiler processes.
4. Open `CC_LOGGER_FILE` read/write (create, mode 0600, no truncation).
5. Working directory = canonical absolute current directory.
6. Dispatch by program name (`prog`):
   - matches `CC_LOGGER_GCC_LIKE` → GCC-like parser (with logging switched
     off for any child process it starts: every `LD_PRELOAD=` entry of
     `environ` is renamed to `XD_PRELOAD=` and restored afterwards);
   - matches `CC_LOGGER_JAVAC_LIKE` → javac parser (logs Java compile actions;
     not used by C/C++ analysis);
   - else log INFO "does not match any program name" and write nothing.
7. Append the produced actions to the file (see §2.4), unlock.

### Program list matching

`CC_LOGGER_GCC_LIKE` is split at `:`. For each token:
- token contains `/`: drop one leading `/`; match if the program path equals
  the token, or ends with `/<token>` (suffix on a path-component boundary).
  So `/cc` matches `cc`, `/usr/bin/cc`, `bin2/cc`, but not `gcc` or `ccache`.
- otherwise: match if the token is a **substring** of the program's base
  name. So `gcc` also matches `x86_64-linux-gnu-gcc-14`, and `cc` would
  match `ccache` if it were a token.

### GCC-like parser

Builds one action `{arguments, sources, output}`:

1. `arguments[0]`: if `prog` is not absolute, search each `PATH` directory for
   an existing file `<dir>/<prog>` (no symlink resolution, so ccache
   symlinks are kept); use that path if found, else `prog` as is.
2. Language (only for implicit include env vars below): C if the tool's base
   name contains `gcc`, `cc` or `clang`; then C++ if it contains `g++`, `c++`
   or `clang++` (C++ wins). Default C++. `-x c`/`-x c-header` → C,
   `-x c++`/`-x c++-header` → C++.
3. For `argv[1..]`:
   - append every non-empty argument to `arguments`;
   - remember the position after the last `-I...` and `-isystem...` (used
     for insertion below);
   - `-o X` / `-oX` → output path (made absolute; default `./_noobj`);
   - a non-option argument whose extension (lower-cased, after the last `.`
     of the base name) is one of `c cc cp cpp cxx c++ o so a` is a source.
     It is stored as written, or made absolute if `CC_LOGGER_ABS_PATH` is
     set. Duplicates are ignored.
4. If `CC_LOGGER_DEF_DIRS` is set: run `<prog> -xc++ -E -v - < /dev/null 2>&1`
   (always as C++), collect the lines between
   `#include <...> search starts here` and `End of search list`, make them
   absolute, skip paths containing `/lib/gcc` followed later by `include`,
   and insert `-I<path>` items after the last `-I`.
5. If `CPATH` is set: insert `-I <p>` pairs (empty component → `.`) after the
   last `-I`.
6. If C++ and `CPLUS_INCLUDE_PATH` is set (or C and `C_INCLUDE_PATH`):
   insert `-isystem <p>` pairs after the last `-isystem`.
7. If `CC_LOGGER_ABS_PATH` is set: for `-I`, `-idirafter`, `-imultilib`,
   `-iquote`, `-isysroot`, `-isystem`, `-iwithprefix`,
   `-iwithprefixbefore`, `-sysroot`, `--sysroot`, make the path absolute,
   both in joined (`-Ipath`, `--sysroot=path`) and separate (`-I path`)
   form.
8. Remove the output path from the sources (handles `-MT x.o`).
9. Unless keep-link (`CC_LOGGER_KEEP_LINK=true`): remove all sources with
   extension `o`, `so`, `a`.
10. If sources remain → one action. Else, if an argument starts with `@`
    (response file) → one action with that `@file` as its source. Else warn
    `No source file was found.` and write nothing.

Absolute-path helper: relative paths are prefixed with the current directory;
then `realpath` is tried; if the path does not exist, the parent is resolved
recursively and the last component re-appended (so non-existing files still
get a canonical directory part).

### Output format

The file holds a JSON array. Writing an action:

- File size 0 → write `[\n`.
- Otherwise seek to the last byte (the `]`) and overwrite it. If the file is
  larger than 5 bytes, it already has entries, so a separator is needed.
- For **each source** of the action, write one entry (separator `\t,\n`
  between entries):

  ```
  \t{\n
  \t\t"directory": "<working dir>",\n
  \t\t"command": "<command>",\n
  \t\t"file": "<source>"\n
  \t}\n
  ```

- Finally write `]`.

`<command>` is the arguments joined with single spaces, each argument
**shell-escaped then JSON-escaped** in one step:

| Input character | Written as (raw file bytes) | After JSON decoding |
|---|---|---|
| space, `\t`, `\n`, `\a`, `\b`, `\f`, `\r`, `\v`, ESC | `\\` + ` `/`t`/`n`/`a`/`b`/`f`/`r`/`v`/`e` | `\ `, `\t`, ... |
| `\` | `\\\\` | `\\` |
| `"` | `\\\"` | `\"` |
| other control char < 0x20 | `\\x` + two hex digits (`0`/`1`, then `0`–`F`) | `\xNN` |
| anything else | unchanged | unchanged |

So the decoded `command` is a valid shell command line. Example (verified):
argument `-DMSG="a b"` is written as `-DMSG=\\\"a\\ b\\\"` and decodes to
`-DMSG=\"a\ b\"`, which a shell splits back to `-DMSG="a b"`.

`directory` and `file` are written **without** JSON escaping (paths with `"`
or `\` produce invalid JSON).

### Logger debug output

If `CC_LOGGER_DEBUG_FILE` is set, the library appends diagnostic lines
(timestamp, level, file:line, message) to that file.

## 3. Environment variables (summary)

| Variable | Read by | Meaning |
|---|---|---|
| `CC_LOGGER_FILE` | library | Output JSON file (required) |
| `CC_LOGGER_GCC_LIKE` | library | Compiler match list |
| `CC_LOGGER_JAVAC_LIKE` | library | javac match list |
| `CC_LOGGER_KEEP_LINK` | library | `true` keeps object/library "sources" |
| `CC_LOGGER_ABS_PATH` | library | Absolute sources and include paths |
| `CC_LOGGER_DEF_DIRS` | library | Add compiler default include dirs |
| `CC_LOGGER_DEBUG_FILE` | library, `log` | Debug log |
| `CPATH`, `C_INCLUDE_PATH`, `CPLUS_INCLUDE_PATH` | library | Added as `-I` / `-isystem` |
| `LD_PRELOAD`, `LD_LIBRARY_PATH` | dynamic loader | Set by `log` |

Exit code of `log`: the build's exit code (0 on success), 1 on setup errors.
