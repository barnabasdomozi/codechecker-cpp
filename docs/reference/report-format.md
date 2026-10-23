# Report model and result file formats (shared)

The in-memory report model used by every command, and the plist result file
format written by `analyze` and `report-converter`. SARIF reading/writing is
in [parse-export.md](parse-export.md) and
[report-converter.md](report-converter.md).

Python source: `tools/report-converter/codechecker_report_converter/report/`
(`__init__.py`, `parser/plist.py`, `parser/base.py`, `report_file.py`,
`error_file.py`).

## 1. Model

```
File        { id, path (possibly trimmed), original_path, content? }
              equality and hashing by id (= original path by default)
Range       { start_line, start_col, end_line, end_col }
BugPathPosition { file, range? }
BugPathEvent    { file, line, column, message, range? }
MacroExpansion  { BugPathEvent fields, name }       message = expansion text
SourceReviewStatus { status="unreviewed", message=bytes, bug_hash, in_source=false,
                     author?, date? }
Report {
  analyzer_result_file_path, file, line, column, message, checker_name,
  severity?, report_hash?, analyzer_name?, category?, type?,
  bug_path_events      (default: one event at the report location with the message),
  bug_path_positions   (default: []),
  notes, macro_expansions, annotations: {str: str}?,
  static_message       (default: message; used for hashing),
  review_status        (default: unreviewed),
  source_line          (lazy: line `line` of `file`; or the "changed" warning, see §4)
}
```

Report equality / hash: (file id, line, column, message, checker name,
report hash).

`trim_path_prefixes(prefixes)`: applied to the main file and all event,
position, note and macro files: removes the **longest** matching prefix. Each
prefix gets a trailing `/` if missing, is converted from a shell glob
(`*`, `?`, `[...]`, where `*` also matches `/`) into a regex anchored at the
start, and the matched text is the prefix length. A match equal to `/` is
ignored.

Unknown checker name constant: `("UNKNOWN", "NOT FOUND")`; a diagnostic
without `check_name` gets checker name `NOT FOUND`. Placeholder checker
`("__FAKE__", "__FAKE__")` is used by the server.

## 2. Plist result file

A standard Apple XML property list (as written by Python `plistlib`, i.e.
XML 1.0, UTF-8, DOCTYPE `-//Apple//DTD PLIST 1.0//EN`, tab-indented
`<dict>`/`<array>`). Root dictionary:

```
{
  "files": [ "<abs path 0>", "<abs path 1>", ... ],     sorted, unique (original paths)
  "diagnostics": [ <diagnostic>, ... ],
  "metadata": {
    "generated_by": { "name": "CodeChecker", "version": "6.30.0" },   // or report-converter's own name/version outside CodeChecker
    "analyzer":     { "name": "<analyzer>" }                          // when known
  }
}
```

Diagnostic:

```
{
  "location": { "line": L, "col": C, "file": <index into files> },
  "issue_hash_content_of_line_in_context": "<report hash>",
  "check_name": "<checker>",
  "description": "<message>",
  "category": "<category or 'unknown'>",
  "type": "<analyzer name>",                         // only if known
  "path": [ <control>..., <event>... ],
  "notes": [ { "location": {...}, "message": "...", "ranges": [[loc, loc]] } ],     // only if any
  "macro_expansions": [ { "name": "...", "expansion": "...", "location": {...} } ], // only if any
  "report-annotation": { "<key>": "<value>", ... }   // only if any
}
```

Writing `path`:
1. Control items:
   - if the report has bug path positions: for each consecutive pair (a, b)
     where both have ranges, one item
     `{"kind": "control", "edges": [{"start": range(a), "end": range(b)}]}`;
   - else if it has more than one event: for each consecutive pair of events,
     using each event's range (or a zero-width range at line/column), skip
     pairs with equal ranges, else emit a control item as above.
   - `range(x)` = `[{"line": sl, "col": sc, "file": i}, {"line": el, "col": ec, "file": i}]`.
2. Event items for every bug path event:
   `{"kind": "event", "location": {...}, "depth": 0, "message": "...", "ranges": [range]}`
   (`ranges` only if the event has a range).

Reading a plist (`get_reports(file, source_dir)`):
- Parse failure → warning `Invalid plist file` (or the lxml syntax error
  message), no reports.
- `files[i]` is normalized as `normpath(join(source_dir, files[i]))`;
  `source_dir` defaults to the plist's directory (so relative paths are
  relative to the plist). Equal paths share one `File` object (file cache).
- Per diagnostic:
  - `checker_name` = `check_name` or `NOT FOUND`;
  - `analyzer_name` = `metadata.analyzer.name`, else `clang-tidy` if the
    checker starts with `clang-diagnostic-`, else none;
  - `severity` from the checker labels (when labels are available);
  - `report_hash` = `issue_hash_content_of_line_in_context`, or the
    path-sensitive hash if missing ([report-hash.md](report-hash.md));
  - events: every `path` item with `kind == "event"`; location from
    `location`; range from `ranges[0]` if present, else the location;
  - positions: from `kind == "control"` items: for each item take
    `edges[0]`; add the edge's `start` range unless the previous control
    item's `end` equals this item's `start` (avoids duplicates in A→B, B→C);
    then add the `end` range. (The position's file is taken from the
    **second** location of the range.) Items with empty `edges` are skipped;
  - notes: `notes` with non-empty `message`;
  - macro expansions: `macro_expansions` with non-empty `expansion`;
  - annotations: `report-annotation` dictionary.
- Errors (missing file index, etc.) → warning, the reports read so far are
  returned.

## 3. Result folder files

| File | Format / content |
|---|---|
| `*.plist` | result files (above) |
| `*.plist.err` | error side file for failed analyzer runs, see [analyzer-drivers.md](analyzer-drivers.md) §6 |
| `*.sarif` | also read as result files by `parse`/`store` (SARIF parser) |
| `metadata.json` | see [analyze.md](analyze.md) §5 |
| `compile_cmd.json`, `unique_compile_commands.json`, `compiler_info.json` | see [analyze.md](analyze.md) |
| `skip_file`, `review_status.yaml`, `conf/` | copies of config files |
| `fixit/*.yaml` | clang-tidy `--export-fixes` output |
| `ctu_connections/<result name>` | CTU-involved files, one per line |
| `failed/*.zip`, `reproducer/*.zip`, `success/*.txt` | see [analysis-debugging.md](analysis-debugging.md) |

`parse`/`store` find result files by walking the folder **recursively**
(`os.walk`) and taking every file ending in `.plist` or `.sarif`. That is why
analyzer raw outputs are renamed to `.bak`.

## 4. Changed-file detection

`report.changed_files` = original files of the report (main + all event/note
/macro files) that are missing, have no readable modification time, or were
modified **after** the result file (`mtime(file) > mtime(result file)`). If
the result file's mtime cannot be read, the result file itself is added.

If the main file is changed, `source_line` becomes
`WARNING: source file content is changed or missing. Please re-analyze your project to update the reports.`
and source-code review comments are not read for it.
