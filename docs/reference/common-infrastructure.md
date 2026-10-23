# Shared infrastructure

Behavior shared by every subcommand: the launcher, subcommand discovery,
argument parsing, config-file splicing, logging, table output and package data
files. Other reference pages refer to this one.

Python source: `bin/CodeChecker` (launcher), `codechecker_common/cli.py`,
`codechecker_common/cmd_config.py`, `codechecker_common/logger.py`,
`tools/report-converter/codechecker_report_converter/twodim.py`,
`codechecker_common/output/__init__.py`.

## 1. Package layout

An installed package has this layout (the "data files directory" is the
package root):

```
<root>/
├── bin/CodeChecker              launcher
├── bin/<tools>                  report-converter, tu_collector, ldlogger, ...
├── lib/python3/...              implementation
├── ld_logger/lib/{32bit,64bit}/ldlogger.so
├── config/
│   ├── analyzer_version.json    version info of the analyzer part
│   ├── web_version.json         version info of the web part
│   ├── package_layout.json      default analyzer binaries, replacer binary
│   ├── config.json              names of logger environment variables
│   ├── labels/analyzers/*.json  checker labels (severity, profile, guideline, ...)
│   ├── guidelines/*.yaml        guideline rule lists
│   ├── server_config.json       template copied into new server workspaces
│   ├── session_client.json      template for client credential file
│   ├── system_comment_kinds.json
│   └── git_commit_urls.json
└── www/                         built web UI
```

`package_layout.json`:

```json
{
  "runtime": {
    "analyzers": {"clangsa": "clang", "clang-tidy": "clang-tidy",
                  "cppcheck": "cppcheck", "gcc": "g++", "infer": "infer"},
    "clang-apply-replacements": "clang-apply-replacements"
  },
  "ld_lib_path_extra": [],
  "path_env_extra": []
}
```

A value without a directory part is looked up in `PATH`. A value with a
directory part is relative to the package root.

## 2. Launcher

`bin/CodeChecker` is a thin wrapper:

1. Saves a copy of the current environment to a temporary file and sets
   `CODECHECKER_ORIGINAL_BUILD_ENV=<that file>`. `CodeChecker log` and the
   analyzers later run the build / compilers in this **original**
   environment, not in the modified one (see [log.md](log.md)).
2. Sets `CC_BIN_DIR=<root>/bin` (and `PYTHONPATH` for the Python version).
3. Runs the real entry point with all arguments, forwards `SIGINT`,
   `SIGTERM`, `SIGHUP` and `SIGCHLD` to the child, deletes the temporary
   folder and exits with the child's exit code.

The entry point:
- Forces UTF-8 for stdout/stderr; characters that cannot be encoded are
  written as backslash escapes.
- Sets `CC_LIB_DIR` (implementation dir) and `CC_DATA_FILES_DIR` (= parent of
  `CC_BIN_DIR`) if not set.
- Installs `SIGINT`/`SIGTERM` handlers that exit with `128 + signum`.
- Catches `KeyboardInterrupt` → prints `Interrupted by user...`, exit 1.
  Any other uncaught exception → stack trace on **stdout**, exit 1.

## 3. Subcommands

Subcommands are the modules of these packages, with `_` replaced by `-`:

| Package | Subcommands |
|---|---|
| common | `version` |
| analyzer | `analyze`, `analyzer-version`, `analyzers`, `check`, `checkers`, `fixit`, `log`, `parse` |
| web | `web-version` |
| server | `server` |
| client | `cmd`, `store` |

When `argv[1]` names a known subcommand, only that one is loaded (faster
start). Without a subcommand, the top-level help is printed.

## 4. Argument parsing rules

All parsers behave like Python `argparse` with these specifics:

- **Parse errors exit with code 1**, not 2: usage is printed to stderr, then
  `<prog>: error: <message>`. Code 2 means "reports found" in several
  commands.
- `allow_abbrev=False` on most subparsers: long options cannot be abbreviated.
- Many options use "suppress" defaults: if the user does not give them, the
  attribute does not exist at all. Code checks "was it given?" by presence,
  not by value. A port needs a tri-state (absent / given) for these options.
- Every subcommand has `--verbose {info,debug_analyzer,debug}`.

## 5. Config file splicing (`--config`)

Commands with `--config FILE`: `analyze`, `check`, `parse`, `server`,
`store`.

Algorithm (in the entry point, after the first parse):

1. If the subcommand supports config files, set up logging (to stderr) using
   `--verbose`.
2. If `--config` was given and the file exists:
   - `*.yaml`/`*.yml` → YAML, loaded with all scalars as **strings**;
     otherwise JSON.
   - Select the option list:
     - `analyze`: section `analyze`; if empty, section `analyzer` (legacy). If
       both are present, warn and use `analyze`.
     - `check`: analyze options (as above) followed by section `parse`.
     - any other subcommand: the section with the subcommand's name
       (`argv[1]`, so `server`, `store`, `parse`; for `cmd` it would be `cmd`).
   - Log `Extending command line options with <cmd> options from '<file>' file: <options>`.
3. Expand environment variables (`$VAR`, `${VAR}`) in every option string.
4. **Replace** the two tokens `--config FILE` in argv with the options, at
   the same position, and parse again. Log `Full extended command: <argv>`.

Consequences:
- Options before `--config` on the command line are overridden by the same
  option in the file (last one wins). Options **after** `--config` win over
  the file.
- Only the form `--config FILE` (two tokens) is supported, not
  `--config=FILE`.
- A section entry can be `--opt=value` or two entries `--opt`, `value`.
- If `--config` is given but the file does not exist, nothing is spliced; the
  command itself later fails with
  `Configuration file '<file>' does not exist.` (exit 1) where it calls the
  check (`server`, `store`, `analyze`, `parse`).

## 6. Logging

Log levels: `DEBUG` (10), `DEBUG_ANALYZER` (15, custom), `INFO` (20),
`WARNING`, `ERROR`, `CRITICAL`. `--verbose` sets every logger and handler to
that level; invalid values become `INFO`.

Formats:
- Normal (`info`): `[LEVEL YYYY-MM-DD HH:MM:SS] - message`
- `debug` / `debug_analyzer`:
  `[LEVEL] [YYYY-MM-DD HH:MM:SS] {logger-name} [pid] <thread-id> - file.py:line function() - message`

Logger names: `system`, `analyzer`, `analyzer.clangsa`, `analyzer.tidy`,
`buildlogger`, `profiler`, `report`, `server`, `report-converter`.

Output stream: stdout by default. Commands that print machine-readable data
(JSON/CSV output, version in non-table formats, config-file processing)
switch the log stream to **stderr** so stdout stays clean.

`CC_LOG_CONFIG_PORT`: if set, the server starts a log-configuration listener
on that port (Python `logging.config.listen`) for changing log config at run
time. Optional for a port.

## 7. Two-dimensional output formats (`twodim`)

Used by every command with `-o {rows,table,csv,json,plaintext}`:

| Format | Rendering |
|---|---|
| `table`, `plaintext` | Box table, single-line borders (`┌─┬─┐ │ ├─┼─┤ └─┴─┘`), left-aligned, header row separated. |
| `rows` | No header. One line per row: a leading space, then cells separated by a space. All columns except the first and last are left-padded to the column's maximum width. |
| `csv` | Header + rows, cells joined with `,`. **No quoting or escaping**: cells containing commas break the column structure. |
| `json` | Array of objects, `{header[i]: cell[i]}`. |

Table width fitting: the terminal width is `$COLUMNS`, else the real terminal
width, else 80. If the natural table width (`sum(widths) + 3*cols + 1`) fits,
print as is. Otherwise:
1. available = `max(width - overhead, cols*4)`;
2. repeatedly lock columns whose natural width ≤ `remaining / unlocked`;
3. split what remains among unlocked columns proportionally to their natural
   width (minimum 4);
4. cells are wrapped (word-wrapped) inside these widths.

An optional "separate footer" mode draws a rule between every row (used for
tables whose last row is a total, e.g. `cmd sum`).

`None` cells print as empty strings.

## 8. Common JSON helper

`load_json(path, default)` returns `default` (and logs a warning unless
disabled) if the file is missing or invalid JSON. Many commands rely on this
"soft failure" behavior.
