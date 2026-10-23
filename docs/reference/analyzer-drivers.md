# Analyzer drivers (shared by `analyze`, `check`, `checkers`, `analyzers`)

How each of the five analyzers lists its checkers and options, builds its
command line, and turns its output into a result file. Binary lookup and
version checks are in [analyzers.md](analyzers.md). Checker on/off state
comes from [checker-selection.md](checker-selection.md).

Python source: `analyzer/codechecker_analyzer/analyzers/` (`analyzer_base.py`,
`config_handler.py`, `flag.py`, `clangsa/`, `clangtidy/`, `cppcheck/`,
`gcc/`, `infer/`, `result_handler_base.py`).

## 1. Common inputs: the build action

Each analysis job gets a **build action** (see
[compiler-info.md](compiler-info.md) for how it is built from a compilation
database entry) and an analyzer name. Fields used here:

| Field | Meaning |
|---|---|
| `source` | Absolute path of the source file |
| `directory` | Working directory of the compile command (the analyzer runs here) |
| `original_command` | The compile command as a string |
| `analyzer_options` | Filtered compiler flags to forward |
| `lang` | `c`, `c++`, `objective-c`, `objective-c++`, ... |
| `target` | Value of `--target`/`-target` in the command, else empty |
| `arch` | Value of `-arch`, else empty |
| `compiler_includes` | Implicit include dirs of the original compiler |
| `compiler_standard` | Implicit `-std=...` of the original compiler, or empty |

Helper rules used below:
- `has_flag(f, cmd)`: some element of `cmd` **starts with** `f`.
- `prepend_all(f, [a, b])` = `[f, a, f, b]`.
- Implicit include flag: `-isystem` if `--add-gcc-include-dirs-with-isystem`,
  else `-idirafter`.

The analyzer process runs with cwd = `directory`, in a new process group
(so the whole group can be killed), with the environment from
`get_env_for_bin` ([analyzers.md](analyzers.md) §4). stdout and stderr are
captured fully (decoded as UTF-8, undecodable bytes dropped).

## 2. Result file naming

For a build action and analyzer:

```
action_hash  = md5_hex( normpath(join(directory, source)) + "_" + " ".join(args) )
               where args = shell-split(original_command) with every "-o X" pair
               and every "-oX" token removed
result_name  = basename(source) + "_" + analyzer + "_" + action_hash
result_file  = <output_dir>/<result_name>.plist
fixit_file   = <output_dir>/fixit/<result_name>.yaml
```

Verified: `/…/proj/main.c` with command `/usr/bin/gcc -c main.c -o main.o`
gives `md5("/…/proj/main.c_/usr/bin/gcc -c main.c")` =
`681db965dea6200719ba62bde2ba8cb4` →
`main.c_clangsa_681db965dea6200719ba62bde2ba8cb4.plist`.

Note: re-joining split arguments with spaces loses quoting, so commands that
differ only in quoting get the same hash.

## <a name="checker-lists"></a>3. Checker lists

| Analyzer | How the checker list is obtained | Names |
|---|---|---|
| clangsa | `<clang> -cc1 [-load <plugin>...] -analyzer-checker-help [-analyzer-checker-help-alpha] [-analyzer-checker-help-developer]` (the extra flags only if they appear in `<clang> -cc1 --help`; `-alpha` is always requested, `-developer` only for the statistics capability check). Parse the text after `CHECKERS:` (format below). | as printed, e.g. `core.DivideZero` |
| clang-tidy | `<clang-tidy> -list-checks -checks=*`: every line that is a single token, skipping `Enabled checks:`, empty lines and `clang-analyzer-*`. Plus `clang-diagnostic-<w>` for every warning `w` from `diagtool tree` (lines starting with `-W`, excluding plain `-W`, minus `blacklist:true` checkers), plus `clang-diagnostic-error`. Cached per process. | `bugprone-...`, `clang-diagnostic-unused-variable` |
| cppcheck | `<cppcheck> --errorlist` (XML): every `errors/error` element; name `cppcheck-<id>`, description `msg`. Plus every checker of the `cppcheck` label file not already present. | `cppcheck-nullPointer` |
| gcc | `<g++> --help=warning`: every line whose first token starts with `-W`, has no `=` and is not `-W` itself; name = token with the first `-W` replaced by `gcc-`; description = rest of the line; minus `blacklist:true`. | `gcc-analyzer-null-dereference` |
| infer | `<infer> help --list-issue-types` (env `TZ=UTC`): lines with ≥ 7 `:`-separated fields; name `infer-` + field 0 lower-cased with `_` → `-`; description from label `description` or `used by '<field 6 or 5>' checker`. | `infer-null-dereference` |

Clang help page parser (used for checkers and options):
- Skip everything up to and including the start label (`CHECKERS:` or
  `OPTIONS:`); if absent → empty list.
- A line matching `^\s{2}(\S+)(\s([^\n]+))?$` starts a new entry (flag, and
  optional description start).
- A line matching `^\s{3,}([^\n]+)$` continues the description.
- An entry is emitted only if it has a non-empty description; description
  parts are stripped and joined with spaces.

`diagtool` lookup: next to the resolved clang-tidy binary (if clang-tidy is a
ccache symlink, search the rest of `PATH` after its directory for the real
one); also accepts `diagtool-<major>`. Missing → warning
`'diagtool' can not be found next to the clang binary (<path>)!` and no
`clang-diagnostic-*` checkers except `clang-diagnostic-error`.

## 4. Option lists (`analyzers --analyzer-config`, `checkers --checker-config`)

| Analyzer | Analyzer options | Checker options |
|---|---|---|
| clangsa | `<clang> -cc1 [-load ...] -analyzer-config-help`, section `OPTIONS:`, + `cc-verbatim-args-file` | `<clang> -cc1 [-load ...] -analyzer-checker-option-help [-…-alpha] [-…-developer]`, section `OPTIONS:`; each flag `checker:option` |
| clang-tidy | `<clang-tidy> -dump-config -checks=*`, every line `^(\S+):\s+(\S+)$` (key, value-as-doc), + `cc-verbatim-args-file`, `take-config-from-directory` | same dump: old format `key: X value: Y` pairs (≤ LLVM 14), else YAML `CheckOptions` mapping. In each key **every** `.` is replaced by `:`, then the key is split at the first `:` into checker and option (so `a.b.c` gives checker `a`, option `b:c`) |
| cppcheck | fixed: `addons`, `libraries`, `platform`, `inconclusive`, `cc-verbatim-args-file` | none |
| gcc | fixed: `cc-verbatim-args-file` | none |
| infer | fixed: `cc-verbatim-args-file` | none |

`cc-verbatim-args-file` values must be existing paths (validated in
`analyze`).

## 5. Command construction

### clangsa

```
<clang> --analyze -Qunused-arguments
  [-Xclang -plugin -Xclang checkercfg -Xclang -load -Xclang <plugin.so>]...   (per plugin)
  -Xclang -analyzer-output=plist-multi-file -o <result_file>
  -Xclang -analyzer-config -Xclang expand-macros=true
  [<extra checker cfg>...]                       (statistics: APIMetadataPath, see below)
  [-Xclang -analyzer-config -Xclang <opt>=<val>]...  (each --checker-config "chk:opt=val"
                                                      and each --analyzer-config opt=val)
  -Xclang -analyzer-checker=<enabled1>,<enabled2>,...      (only if any enabled)
  -Xclang -analyzer-config -Xclang aggressive-binary-operation-simplification=true   (clang >= 8)
  [-Xclang -analyzer-config -Xclang crosscheck-with-z3=true]                         (z3 refutation on)
  [CTU block]
  [-x <lang>]                                     (if no -x yet)
  [-arch <arch>]                                  (if no -arch yet and arch set)
  <verbatim args from cc-verbatim-args-file>
  <analyzer_options>
  [--target=<target>]                             (if no --target/-target yet and target set)
  [<compiler_standard>]                           (if no -std/--std yet and non-empty)
  <implicit include flag> <dir> ...               (compiler_includes)
  <source>
```

CTU block (when CTU dir is configured and CTU not disabled for this run):

```
-Xclang -analyzer-config -Xclang experimental-enable-naive-ctu-analysis=true
-Xclang -analyzer-config -Xclang ctu-dir=<output>/ctu-dir/<triple-arch>
[-Xclang -analyzer-config -Xclang display-ctu-progress=true]     (if listed in -analyzer-config-help)
[-Xclang -analyzer-config -Xclang ctu-invocation-list=<ctu-dir>/<arch>/invocation-list.yml]   (on-demand mode)
```

Disabled checkers are **not** passed (Clang runs dependencies itself).
Instead, after the run (`post_analyze`), the plist is loaded and every
diagnostic whose `check_name` is a disabled checker is removed; the plist is
rewritten. An unreadable plist (parallel duplicate actions) is left as is.

Plugins: `.so` files in `<root>/plugin`, or in `CC_CLANGSA_PLUGIN_DIR` when
`CC_ANALYZERS_FROM_PATH` is set (none if that variable is unset in this mode).

### clang-tidy

```
<clang-tidy>
  -checks=<list>  |  --allow-no-checks (clang-tidy >= 19, when <list> is empty)
  <verbatim args>                              (with any "-config X"/"-config=X" removed, see below)
  [-config=<json>]                             (if the config JSON is not "{}" / empty)
  <source>
  --export-fixes <fixit_file>
  --
  -Qunused-arguments
  [-Wno-everything]                            (unless --enable-all)
  [-x <lang>] [--target=<t>] [-arch <a>]       (same "if absent" rules)
  <analyzer_options>
  <implicit include flag> <dir> ...
  [<compiler_standard>]                        (if no -std/--std yet)
  --enable-all:  -Weverything  + only the "-Wno-..." items of the warning list
  otherwise:     the warning list
```

Checks list and warning list, from the checker states (in state-table order):
- For every `clang-diagnostic-<w>` checker: enabled → `-W<w>`, except
  `clang-diagnostic-error` which gives `-Wno-error`; disabled → `-Wno-<w>`.
- `<list>` = `-*`, then `darwin-dispatch-once-nonstatic` if **all** enabled
  checkers are `clang-diagnostic-*` (a dummy so clang-tidy has at least one
  check), then every enabled checker, with `*` appended if the name is a
  group prefix of another known checker (prefix on `-`-separated parts, e.g.
  `bugprone-string` is a prefix of `bugprone-string-constructor` but not of
  `bugprone-stringview-nullptr`).
- `<list>` is empty (so `-checks` is not passed) when
  `take-config-from-directory=true`, or when the config JSON has a
  non-empty `Checks` key (from `--analyzer-config clang-tidy:Checks=...`).

Config JSON (`-config=`):
1. Start with all `--analyzer-config clang-tidy:K=V` pairs (except
   `cc-verbatim-args-file`) as `{K: V}`.
2. If that is empty: `{"HeaderFilterRegex": ".*"}` (show reports in headers).
3. If the verbatim args contain `-config X` or `-config=X`, parse `X` as JSON,
   merge it over the map (verbatim wins) and remove those tokens from the
   verbatim args (first occurrence only).
4. If any `--checker-config clang-tidy:C:O=V` exists, set
   `CheckOptions = [{"key": "C.O", "value": "V"}, ...]`.
5. If `take-config-from-directory` is `true`, no `-config` is passed (and
   `clang-diagnostic-*` checkers are not even added to the state table).
   Otherwise the JSON (Python `json.dumps` format: `{"a": "b"}`) is passed.

Output: clang-tidy prints diagnostics to stdout. The result handler parses
stdout with the report-converter clang-tidy parser (see
[report-converter.md](report-converter.md)) and writes the plist itself.

### cppcheck

```
<cppcheck> --enable=all
  --suppress=<id> ...            (every disabled checker, "cppcheck-" prefix removed)
  [--suppress=unusedFunction]    (always, if not already)
  <verbatim args>
  <whitelisted flags>            (see below; the --std item is dropped if verbatim args contain --std=)
  --platform=<first "platform" value> | --platform=native
  [--inconclusive]               (if "inconclusive" option given, any value)
  [--addon=<a>]...  [--library=<l>]...
  --plist-output=<output>/cppcheck/<action_hash>
  <source>
```

Whitelisted flags from `analyzer_options`: anything matching `-[IUD].*`
(if the token is exactly `-I`/`-U`/`-D`, the next token too), and the standard
flag (`--std`, `-std=`, `--std=`) converted to `--std=<v>` where `v` is
lower-cased, `gnu` replaced by `c`, then mapped:
`c90→c89, c18→c17, iso9899:2017→c17, iso9899:2018→c17, iso9899:1990→c89,
iso9899:199409→c89, c9x→c99, iso9899:1999→c99, iso9899:199x→c99, c1x→c11,
iso9899:2011→c11, c2x→c23, iso9899:2024→c23, c++98→c++03, c++0x→c++11,
c++1y→c++14, c++1z→c++17, c++2a→c++20, c++2b→c++23, c++2c→c++26`.

Post-processing: every `*.plist` under `<output>/cppcheck/<action_hash>/` is
copied to the result file and renamed to `*.plist.bak`. The result handler
then reads it, prefixes checker names with `cppcheck-`, drops reports whose
checker is not in the checker list (informational messages), appends a final
bug path event (message, file, line, column, zero-width range) if the last
event differs, computes hashes (default: path-sensitive, see
[report-hash.md](report-hash.md)) and rewrites the plist.

### gcc

```
<g++> -fanalyzer -c -o/dev/null
  <verbatim args>
  <analyzer_options>
  -fdiagnostics-format=sarif-stderr
  -W<name> | -Wno-<name>      (for EVERY checker in the state table: enabled → "gcc-"→"-W", disabled → "gcc-"→"-Wno-")
  [-x <lang>]
  <verbatim args>             (again)
  <source>
```

Post-processing: stderr (SARIF JSON) is written to
`<output>/gcc/<basename(source)><action_hash>.sarif`, parsed with the SARIF
parser, the file renamed to `.sarif.bak`; only reports whose rule starts with
`-Wanalyzer` are kept and renamed `-W…` → `gcc-…`; hashes computed; plist
written.

### infer

```
infer run --keep-going --project-root /
  --enable-issue-type X | --disable-issue-type X   (every checker; X = name minus "infer-", "-"→"_", upper-case)
  -o <output>/infer/<action_hash>
  <verbatim args>
  --
  <original_command split, minus tokens matching the GCC ignore regex (for c/c++)>
  [-stdlib=libc++]                                   (c++)
```

Run with `TZ=UTC`. Post-processing reads
`<output>/infer/<action_hash>/report.json` with the Infer parser, prefixes
names with `infer-`, computes hashes, writes the plist, deletes the infer
folder. No skip-list filtering is applied to Infer reports.

## 6. Result handling (all analyzers)

After the process ends (see [analyze.md](analyze.md) for success/failure
handling), the result handler:

1. Parses the analyzer output into reports (clangsa/cppcheck: the plist;
   clang-tidy: stdout; gcc: SARIF; infer: JSON).
2. Drops reports for which `report.skip(filter_handlers)` is true (only when
   `--drop-reports-from-skipped-files`, see
   [skip-and-file-filters.md](skip-and-file-filters.md)).
3. Sets report hashes:

   | Analyzer | default | `context-free` | `context-free-v2` | `diagnostic-message` |
   |---|---|---|---|---|
   | clangsa | keep Clang's `issue_hash_content_of_line_in_context` | context-free | context-free | diagnostic-message |
   | clang-tidy, cppcheck, gcc, infer | path-sensitive | path-sensitive | context-free | diagnostic-message |

   (Algorithms in [report-hash.md](report-hash.md).)
4. Drops reports that the review status handler says to ignore (see
   [review-status-and-suppression.md](review-status-and-suppression.md)).
5. Writes the plist file (format in [report-format.md](report-format.md)),
   with severities from the checker labels.
6. Error side file (`error_file.update`): delete `<result_file>.err` if it
   exists; if the analyzer's return code is non-zero, write
   `<result_file>.err` as a plist dictionary
   `{analyzer_name, analyzer_cmd, return_code, stdout, stderr}`, where
   non-printable characters (other than `\n`, `\r`, `\t`) in the strings are
   written as `\xHH`. `analyzer_cmd` is the command **list**; the conversion
   iterates its elements, so the stored value is all arguments concatenated
   with **no separator** (e.g. `/usr/bin/clang--analyze-Qunused-arguments...`).
   The `.err` extension keeps the file out of report parsing.
