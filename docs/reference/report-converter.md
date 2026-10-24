# Reference: `report-converter`

Usage: [../usage/report-converter.md](../usage/report-converter.md)

Python source: `tools/report-converter/codechecker_report_converter/cli.py`,
`.../analyzers/analyzer_result.py` (`AnalyzerResultBase`),
`.../analyzers/parser.py` (`BaseParser`), `.../analyzers/<tool>/`
(one directory per type), `.../report/__init__.py` (`Report`,
`BugPathEvent`, `get_or_create_file`), `.../report/parser/{plist,sarif}.py`.

Related: the report model and plist format
([report-format.md](report-format.md)), hashes
([report-hash.md](report-hash.md)), SARIF reading
([parse-export.md](parse-export.md) "Reading SARIF"). The clang-tidy text
parser below is also used by `CodeChecker analyze`
([analyzer-drivers.md](analyzer-drivers.md)).

## 1. Command line

```
report-converter -t TYPE -o OUTPUT_DIR [-e {plist,sarif}] [--meta [KEY=VALUE ...]]
                 [--filename PATTERN] [-c] [-v] INPUT [INPUT ...]
report-converter --example TYPE
```

Plain `argparse` (not the CodeChecker launcher): parse errors exit **2**.
Logging: logger `report-converter` to **stdout**, format
`[<LEVEL>] - <message>`, level INFO (`-v`: DEBUG).

| Option | Default | Notes |
|---|---|---|
| `-t` | required | one of the types in §4 |
| `-o` | required | created if missing |
| `-e` | `plist` | `plist` or `sarif` |
| `--meta` | none | `nargs='*'`: it also swallows following positional arguments, so put it **after** the inputs |
| `--filename` | `{source_file}_{analyzer}_{file_hash}` | output name without extension |
| `-c` | off | delete `OUTPUT_DIR` first |

`--example TYPE` prints `Example commands to produce a '<TYPE>' output
which 'report-converter' can parse:\n` and the type's example text (or a
WARNING if it has none) and exits 0, ignoring the other required options.

Inputs: every path must exist (else ERROR `File or directory '<p>' does
not exist!` for each, then exit 1). Files are taken as is; directories are
walked recursively and **every** file in them is an input (no extension
filter). If nothing is collected (e.g. an empty directory) the program dies
with an `AssertionError` (exit 1).

`--meta`: each value is split at the first `=` (a value without `=` →
`ValueError` traceback, exit 1). Valid keys: `analyzer_command`,
`analyzer_version`; others → ERROR `The following metadata keys are
invalid: <k1>, <k2>. Valid key values are: analyzer_command,
analyzer_version.`, exit 1.

Exit code 0 otherwise — also when nothing was found.

## 2. Conversion (`AnalyzerResultBase.transform`)

1. `-c` and `OUTPUT_DIR` exists: INFO `Previous analysis results in '<dir>'
   have been removed, overwriting with current result` and delete it.
   Create `OUTPUT_DIR`.
2. For each input file (made absolute): `get_reports(path)` of the type
   (§4). None → INFO `No '<TYPE>' results can be found in '<path>'.`
3. Post-process each report:
   - report hash: **context-free** ([report-hash.md](report-hash.md)) for
     all types except `clang-tidy`, `clang-tidy-yaml` and `pmd`, which use
     **path-sensitive**. This always overwrites a hash read from the input
     (Infer's `hash`, SpotBugs' `instanceHash`, Cppcheck plist hashes);
   - `analyzer_name` = `TYPE`;
   - empty checker name → `TYPE`.
4. Group reports by `normpath(original path of the main file)`. For each
   group, file name = `--filename` with `{source_file}` → base name of the
   file, `{analyzer}` → `TYPE`, `{file_hash}` → MD5 hex of the normalised
   path (UTF-8, errors ignored); add `.<plist|sarif>`. INFO
   `Create/modify <ext> file: '<out path>'.` and write the reports with
   analyzer info `{name: TYPE}` ([report-format.md](report-format.md) §2;
   plist `metadata` = `{"analyzer": {"name": TYPE}, "generated_by":
   {"name": "report-converter", "version": "0.1.0"}}`). An existing file is
   overwritten.
5. With valid `--meta`: write `OUTPUT_DIR/metadata.json` =
   `{"version": 2, "num_of_report_dir": 1, "tools": [{"name": TYPE,
   "version": <analyzer_version>, "command": <analyzer_command>}]}` (keys
   only if given). Without: WARNING `Use '--meta' option to provide extra
   information to the CodeChecker server such as analyzer version and
   analysis command when storing the results to it. For more information
   see the --help.`

   ⚠️ `command` is written as a **string**, but the server joins
   `tools[].command` as a list ([store.md](store.md) §4.3), so
   `--meta analyzer_command="a b"` is stored as `a   b` (a space between
   every character; verified). A port should write a list (e.g. split with
   shell rules) or the server should accept a string.

A `Report` created without bug path events gets one event: its own
message at its own location. All file paths go through a per-run cache
(one `File` object per path string).

## 3. Line-based parsing (`BaseParser`)

Text inputs are read as UTF-8 with undecodable bytes replaced, split into
lines **keeping** the line terminators. Starting with the first line, the
type's `_parse_line(iterator, line)` returns `(reports, next line)`; this
repeats until the iterator is exhausted. Unless said otherwise below, a
line that does not match the type's main regex is skipped.

Regexes below are Python `re` patterns applied with `match` (anchored at
the start, not at the end); `[\S \t]` never matches the line terminator,
so captured messages end before it; messages are `strip()`-ed. "Relative
to the input" = `os.path.join(dirname(input file), path)` (an absolute
`path` stays as is), "normalised" = `os.path.normpath`, "absolute" =
`os.path.abspath` against the current directory.

## 4. Types

### 4.1 Sanitizers (`asan`, `msan`, `tsan`, `lsan`, `ubsan`)

Input: the program's stderr output.

Common algorithm (`SANParser`) for `asan`, `msan`, `tsan`, `lsan`:
1. Header line regex (per type):
   - `asan`: `==(?P<code>\d+)==(ERROR|WARNING): AddressSanitizer: (?P<message>[\S \t]+)`
   - `msan`: same with `MemorySanitizer`; `tsan`: with `ThreadSanitizer`
   - `lsan`: `(?P<message>(Ind|D)irect leak of \d+ byte\(s\) in \d+ object\(s\) allocated from:)`
2. Skip the line after the header (not for `lsan`).
3. Stack trace: consume lines while the line is not blank (after
   stripping). Each consumed line is kept for the note; a line containing
   `(?P<path>[\S]+?):(?P<line>\d+)(:(?P<column>\d+))?` (search) whose `path`
   **exists on disk** gives an event: message = the line right-stripped,
   file = absolute path, line, column (0 if absent).
4. No events → no report (the header is dropped). Otherwise reverse the
   event list (outermost frame first). Main event = the **last** event
   (`tsan`: the **first**). Report at the main event's file/line/column,
   message = header message, checker name per type, bug path = the events
   plus a final event with the message at the report location, one note =
   all consumed stack lines concatenated (with their line terminators) at
   the report location, static message (for hashing) = message with every
   `0x[0-9a-fA-F]+` removed.

Checker names: `AddressSanitizer`, `MemorySanitizer`, `ThreadSanitizer`,
`LeakSanitizer`, `UndefinedBehaviorSanitizer`. For `asan` the message is
searched with these patterns in order, the first match gives
`AddressSanitizer.<name>`, none → `AddressSanitizer`:
`double-free`, `new-delete-type-mismatch`, `bad-free`,
`alloc-dealloc-mismatch`, `bad-malloc_usable_size`,
`bad-__sanitizer_get_allocated_size`, `calloc-overflow`,
`reallocarray-overflow`, `pvalloc-overflow`,
`invalid-allocation-alignment`, `invalid-aligned-alloc-alignment`,
`invalid-posix-memalign-alignment`, `allocation-size-too-big`,
`rss-limit-exceeded`, `out-of-memory`,
`string-function-memory-ranges-overlap`, `negative-size-param`,
`bad-__sanitizer_annotate_contiguous_container`,
`bad-__sanitizer_annotate_double_ended_contiguous_container`,
`odr-violation`, `invalid-pointer-pair`, `generic-error`
(`.* on address .* at pc .* bp .* sp .*`). The exact patterns are in
`sanitizers/address/parser.py` (`check_msg_pairs`); they are regular
expressions (several contain unescaped `(`, `*`, `.` and must be copied
verbatim to get the same matches).

`ubsan`:
1. Header: `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): runtime error: (?P<message>[\S \t]+)`;
   report location = header path (absolute), line, column.
2. Next line, then consume lines while they match `^\s+#\d+` (stack
   frames), building events as above (reversed).
3. Checker `UndefinedBehaviorSanitizer.<check>` by searching the message
   with the per-check pattern lists of `sanitizers/ub/parser.py` (`checks`,
   in order: `alignment`, `bool`, `builtin`, `bounds`, `enum`,
   `float-cast-overflow`, `integer-divide-by-zero-or-float-divide-by-zero`,
   `implicit-signed-integer-truncation`,
   `implicit-unsigned-integer-truncation`, `implicit-integer-sign-change`,
   `nonnull-attribute-or-nullability-arg`, `null-or-nullability-assign`,
   `nullability-return-or-returns-nonnull-attribute`, `objc-cast`,
   `object-size`, `pointer-overflow`, `return`, `shift`,
   `signed-integer-overflow`, `unreachable`, `unsigned-integer-overflow`,
   `vla-bound`, `vptr`); none → `UndefinedBehaviorSanitizer`. A report is
   created even without stack frames.

### 4.2 `clang-tidy` (text output)

Regexes:
- message: `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): (?P<severity>(error|warning)):(?P<message>[\S \t]+)\s*\[(?P<checker>.*)\]`
  (greedy: the checker is the text in the last `[...]`)
- note: `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): note:(?P<message>.*)`
- fixit, new style: `^\s*\d*\s*\|\s*(?P<message>\S.*)`; old style: `^\s*(?P<message>\S.*)`

Per message line:
1. Report: absolute path, line, column, message, checker = first of the
   comma-separated names in the brackets; bug path starts empty.
2. Next line → **code block**: if it is a note or message line, stop;
   otherwise it is the source line; the following line must contain `^`
   (the caret line) or the block stops there; then advance one more line.
3. **Fixits**: while the line is neither a message nor a note line: if the
   new-style regex matches, text = its group and column = (index of text in
   the line) − (index of the first `|`) − 1; else the old-style regex,
   column = index of text + 1; else a `ValueError` aborts the whole program
   (`Unexpected line: <line>. Expected a FixIt line!`, exit 1 — e.g. a blank
   line after a diagnostic; verified). Each fixit becomes a **note**
   `"<text> (fixit)"` at the report's file and line with that column.
4. **Notes**: while the line is not a message line: a note line adds a bug
   path event (absolute path, line, column, stripped message) and is
   followed by a code block (step 2); any other line ends the report (that
   line is skipped).
5. Append the final event (the message at the report location). If the
   brackets listed several checkers, emit one copy of the report per
   checker name (category = the name's part before the first `-`).

End of input inside a report finishes it normally.

### 4.3 `clang-tidy-yaml` (`--export-fixes` YAML)

Whole-file YAML. For each item of `Diagnostics`:
- checker = `DiagnosticName`; `DiagnosticMessage` → `FilePath` (absolute),
  `FileOffset`, `Message` (stripped);
- location from the offset: read the file as text (UTF-8, errors ignored,
  newlines untranslated); walk lines counting characters; the line where
  the remaining offset ≤ line length gives `(line number, remaining + 1)`.
  (Offsets are therefore treated as **character** counts, not bytes.) An
  offset past the end crashes;
- each `Replacements` item → note `"<ReplacementText> (fixit)"` at its
  file/offset location;
- each `Notes` item with non-empty `FilePath` → bug path event (stripped
  `Message`) at its file/offset;
- final event = the message at the report location.

### 4.4 `gcc` (SARIF)

Read with the SARIF parser ([parse-export.md](parse-export.md) "Reading
SARIF"); then the common post-processing (context-free hash, analyzer
`gcc`).

### 4.5 `cppcheck` (plist)

Input: a directory (all `*.plist` directly in it) or one `.plist` file;
anything else → ERROR `The given input should be an existing CppCheck
result directory or a plist file.` Plists are read with the plist parser
([report-format.md](report-format.md) §2, no checker labels). For each
report, if its last bug path event differs from an event with the
message at the report location (with a zero-width range at that
position), that event is appended.

### 4.6 `fbinfer` (Infer)

Input: an `infer-out` directory (→ its `report.json`; base directory =
parent of `infer-out`) or the `report.json` file (base = parent of its
directory). Missing → ERROR `Report file does not exist: <path>`.
JSON array; per bug: checker = `bug_type`, message = `qualifier`, line,
column = `max(column, 0)`; file: `file` if it exists, else
`<base>/<file>`, else `/<file>`, else WARNING `No source file found:
<path>` and the bug is skipped. Events from `bug_trace[]`
(`filename`, `description`, `line_number`, `column_number` with the same
path resolution; unresolved events are dropped), then the final event.

### 4.7 Simple line formats

| Type | Regex | Path | Column | Checker |
|---|---|---|---|---|
| `cpplint` | `^(?P<path>[\S ]+?):(?P<line>\d+?):  (?P<message>[\S \t]+)  \[(?P<checker_name>\S+)\]\s` | relative to input, normalised | 0 | the name in `  [...]` (two spaces before `[`); the following ` [<confidence>]` is ignored |
| `pyflakes` | `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<message>[\S \t]+)\s*` | relative to input (not normalised) | 0 | `pyflakes` |
| `golint` | `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?): (?P<message>[\S \t]+)\s*` | relative to input (not normalised) | from line | `golint` |
| `mdl` (markdownlint) | `^(?P<path>[\S ]+?):(?P<line>\d+?): (?P<checker>[\S]+) (?P<message>[\S \t]+)\s*` | relative, normalised | 0 | `checker` group |
| `sphinx` | `^(?P<path>\/[\S ]+?):(?P<line>\d+?):\s(?P<message>[\S \t]+)\s*` (absolute paths only) | normalised | 0 | `sphinx` |
| `kernel-doc` | `^(?P<path>\.[\S ]+?):(?P<line>\d+?):\s(?P<message>[\S \t]+)\s*` (paths starting with `.`) | relative, normalised | 0 | `kernel-doc` |
| `smatch` | `^(?P<path>[\S ]+?):(?P<line>\d+?)\s(?P<function_name>\S+)\s\[(?P<checker_name>smatch\.\S+)\]\s(?P<message>[\S \t]+)\s*` | relative, normalised | 0 | `checker_name` group |
| `coccinelle` | report: `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>(\d+?))-(\d+?):(?P<message>[\S \t]+)\s*`; a line `^Processing (?P<checker_name>[\S ]+)\.cocci$` sets the current checker to `coccinelle.<name>` for following reports | relative, normalised | start column | current checker (before the first `Processing` line: empty → `coccinelle`) |
| `sparse` | report: `^(?P<path>[\S ]+?):(?P<line>\d+?):(?P<column>\d+?):(?P<message>[\S \t]+)\s*`; following lines matching `^(?P<path>\.[\S ]+?):(?P<line>\d+?):(?P<column>\d+?):(?P<message>[\S \t]+)\s*` are bug path events | relative, normalised | from line | `sparse` |

Notes: for line formats the paths are not required to exist (the file
is only read later for hashing and output). The `sparse` report regex
also matches the event lines, so an event line that follows a non-matching
line starts a new report.

### 4.8 JSON and XML formats

| Type | Input | Per finding |
|---|---|---|
| `pylint` | JSON array (`pylint -f json`) | file = `path` relative to the input (must exist, else WARNING `Source file does not exists: <p>` and skip), `line`, `column` (as given, 0-based), message = `message`, checker = `symbol` |
| `ruff` | JSON array (`ruff check --output-format json`) | file = `filename` (relative to the input if that exists, else as given; must exist), line/column = `location.row`/`location.column`, `message`, checker = `code` |
| `eslint` | JSON array of `{filePath, messages[]}` | file = `filePath` relative to the input (must exist); per message: `line`, `column` (0 if null/0), `message`, checker = `ruleId` |
| `tslint` | JSON array | file = `name` relative to the input (must exist); line = `startPosition.line + 1`, column = `startPosition.character + 1`, message = `failure`, checker = `ruleName` |
| `pvs-studio` | JSON `{"warnings": [...]}` | for **each** entry of `positions[]` whose `file` exists: one report at `file`, `line`, `column` (0 if absent), message = `message`, checker = `code`, severity from `level` (0 `UNSPECIFIED`, 1 `HIGH`, 2 `MEDIUM`, 3 `LOW`) |
| `jscpd` | `jscpd-report.json` | per `duplicates[]`: report in `firstFile.name` at `firstFile.startLoc.line/column`, message `Duplicated code of <lines> lines. Duplicates <second name>, starting line <second line>`, checker `jscpd`; one note at the second file's start: `Duplicated code of <lines> lines. Duplicates <first name>, starting line <first line>`; missing files only WARNING |
| `pmd` | JSON `{"files": [{filename, violations[]}]}` | file = absolute `filename`; `beginline` (default 1), `begincolumn` (default 1), message = `description`, checker = `rule` (default `unknown`), category = `ruleset`; one event at the report location |
| `spotbugs` | XML (`-xml:withMessages`) | project paths = `Project/{Jar,AuxClasspathEntry,SrcDir}` texts (directories, or the directory of a file). Per `BugInstance`: checker = `type`, message = `LongMessage` text, file from `SourceLine@sourcepath` (as is if it exists, else joined with each project path; not found → WARNING and skip), line = `SourceLine@start` (or the last event line > 0, else 0), column 0; events from child `Class` and `Method` elements (`Message` text at their `SourceLine` start), then the final event |
| `roslynator` | XML | every `CodeAnalysis/Projects/*/Diagnostics/*`: needs `FilePath` (relative to the input, must exist), message = `Message`, line/column = `Location@Line`/`@Character`, checker = `@Id` |

JSON/XML read failures log an ERROR (`Failed to parse the given analyzer
result '<p>'. Please give a valid json file generated by <Tool>.`, or the
XML equivalent; `pvs-studio` logs it as WARNING) and yield no reports.
`pmd` and `clang-tidy-yaml` have no error handling (traceback, exit 1).
Missing input files for the JSON types → ERROR `Report file does not
exist: <p>` (`pvs-studio`: INFO).
