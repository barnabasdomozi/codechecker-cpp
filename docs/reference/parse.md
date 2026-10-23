# Reference: `CodeChecker parse`

Usage: [../usage/parse.md](../usage/parse.md)

Python source: `analyzer/codechecker_analyzer/cli/parse.py`,
`suppress_handler.py`, `suppress_file_handler.py`,
`tools/report-converter/codechecker_report_converter/report/`
(`reports.py`, `output/plaintext.py`, `statistics.py`).

Export formats are in [parse-export.md](parse-export.md); review statuses in
[review-status-and-suppression.md](review-status-and-suppression.md); the
report model and plist reading in [report-format.md](report-format.md).

## 1. Options

| Option | Notes |
|---|---|
| `input...` (positional, ≥ 1) | files or folders; must exist |
| `-t/--type/--input-format` | `plist` only (ignored; `.sarif` files are read too) |
| `-e/--export` | `html`, `json`, `codeclimate`, `gerrit`, `baseline`, `sarif` |
| `-o/--output PATH` | |
| `--suppress FILE`, `--export-source-suppress` | |
| `--print-steps` | |
| `--trim-path-prefix [P...]` | |
| `--review-status [S...]` | values: `confirmed false_positive intentional suppress unreviewed` |
| `--status`, `--detailed` | |
| `-i/--ignore/--skip FILE` | skipfile |
| `--file F...` | |
| `--config FILE` | |

Log stream: stderr when `-e` is given and is not `html`; stdout otherwise.

## 2. Pre-checks (in order)

1. Config file existence ([config-file.md](config-file.md)).
2. Every input must exist: `Input path <p> does not exist!` per missing path,
   exit 1.
3. `-e html` without `-o` →
   `Argument --export not allowed without argument --output when exporting to HTML.`,
   exit 1.
4. `-e gerrit` without `CC_REPO_DIR` and/or `CC_CHANGED_FILES` → one error per
   missing variable (see [parse-export.md](parse-export.md)), exit 1.

## 3. `--status`

Uses only the **first** input, which must be a folder.

1. `-e` other than `json` → `Only JSON export format is supported.`, exit 1.
   `-o` without `-e` → `Export format (--export/-e) was not specified.`,
   exit 1. Not a folder → `Input path '<p>' is not a directory.`, exit 1.
2. Load `<dir>/compile_cmd.json`; missing/empty →
   `Failed to load compile commands JSON '<path>'.`, exit 1. Relative `file`
   entries are made absolute against `directory`.
3. `--file F...`: make each absolute; any non-existing →
   `File filter (--file) contains nonexistent files:` + list, exit 1. Keep
   only entries whose `file` is in the list; none (and no export) → warning
   `File not found in the compilation database!`.
4. For each entry and each of the five analyzers: compute the result file
   name ([analyzer-drivers.md](analyzer-drivers.md) §2, using the entry's
   `file`, `directory`, `command`):
   - `<name>.plist.err` exists in the folder → **failed** (and the command
     counts as analyzed);
   - else `<name>.plist` exists → analyzed; **up-to-date** if
     `mtime(plist) > mtime(source)` (or if the mtimes cannot be read), else
     **outdated**;
   - else **missing**.
   (Only the top level of the folder is checked. Files are counted per
   analyzer and source file; the "analyzed" total counts distinct commands.)
5. Output:
   - no `-e`: INFO lines

     ```
     ----==== Summary ====----
     Up-to-date analysis results
       <analyzer>: <n>                 (only analyzers with n > 0)
     Outdated analysis results
     Failed to analyze
     Missing analysis results
     Total analyzed compilation commands: <n>
     Total available compilation commands: <n>
     ----=================----
     ```

     with `--detailed`, analyzer lines are `  <analyzer>: [<files>] (<n>)`
     (Python list syntax).
   - `-e json`: the JSON below, indent 2, to stdout or to `-o` (INFO
     `Status info was dumped to '<path>'.`).

     ```json
     {"analyzers": {"<analyzer>": {"summary": {"up-to-date": n, "outdated": n, "missing": n, "failed": n},
                                   "up-to-date": [...], "outdated": [...], "missing": [...], "failed": [...]}},   // lists only with --detailed
      "total_analyzed_compilation_commands": n,
      "total_available_compilation_commands": n}
     ```
6. Exit 0.

## 4. Normal mode

1. Review status filter: `--review-status` values, or the default:
   all five values for `-e sarif`, else `confirmed unreviewed`.
2. Suppress handler (`--suppress F`):
   - `F` exists → load it (§6);
   - `F` missing and `--export-source-suppress` → create an empty `F`, INFO
     `Will write source-code suppressions to suppress file: F`, load it;
   - `F` missing otherwise → warning
     `Suppress file 'F' given, but it does not exist -- will not suppress anything.`
   - `--export-source-suppress` without `--suppress` →
     `Can't use '--export-source-suppress' unless '--suppress SUPPRESS_FILE' is also given.`,
     exit 1.
3. Output paths (`-o`): made absolute. For `html` it is a folder. For other
   exports it is a file: an existing **directory** →
   `Please provide a file path instead of a directory for '<export>' export type!`,
   exit 1; `baseline` requires a `.baseline` suffix
   (`Baseline files must have '.baseline' extensions.`, exit 1). The parent
   folder is created.
4. Skip handlers: `--file F...` → a skip list `+F1 ... +Fn -*`
   ([skip-and-file-filters.md](skip-and-file-filters.md) matching); `-i` →
   the skipfile.
5. Iterate result files: for each input, a file → that file (if it ends in
   `.plist`/`.sarif`); a folder → walk it recursively, grouped by
   directory, taking `.plist`/`.sarif` files.
   For each directory group:
   - if `<dir>/review_status.yaml` exists (even a dangling link), load it
     into the review status handler (format error → log, exit 1). Note: one
     handler is reused for all directories; a later directory's file replaces
     the earlier configuration;
   - read `<dir>/metadata.json` if present; with `--file`, restrict the
     result files to those whose `result_source_files` value matches any
     `--file` pattern (Python `fnmatch`); if that gives nothing, keep all;
   - for each result file: read reports (with checker labels for severity
     and a shared file cache); for every report that is not skipped, set its
     review status (§5) — an ambiguity error → log, exit 1;
   - filter (§4.1);
   - count the result file; for each kept report: collect changed files,
     add to statistics, then apply `--trim-path-prefix`;
   - no export → print the reports of this result file (§4.2); html → add a
     page ([parse-export.md](parse-export.md)).
6. Log every source-comment warning (misspelled markers) as WARNING.
7. Output: no export → statistics tables (§4.3); otherwise the export.
8. If any changed files were seen: WARNING
   `The following source file contents changed or missing since the latest analysis:\n - <file>\n...\nPlease re-analyze your project to update the reports!`
9. If the first input is a folder containing `compile_cmd.json`, print the
   status summary (§3 step 5, non-detailed, INFO lines).
10. Exit **2** if at least one report was kept, else 0.

### 4.1 Report filter (in order, per report)

1. Main file skipped by the skip handlers → drop.
2. Suppressed by the suppress file (§6) → drop.
3. With a suppress handler: a report without hash → warning
   `Can't store suppress information for report because no report hash is set: <report>`
   and drop. With `--export-source-suppress`, a report whose review status is
   not `unreviewed` is appended to the suppress file (§6).
4. Review status not in the filter → drop.
5. Duplicate path hash ([report-hash.md](report-hash.md)) already seen in
   this command → drop. (Deduplication spans all result files.)

### 4.2 Text output per result file

Group the kept reports by (trimmed) file path. If the metadata maps this
result file to a source file, that source file is added as a key even
without reports. For each path, sorted case-insensitively:

- reports sorted by line; for each:

  ```
  [<severity>] <last event's file path>:<line>:<column>: <message> [<checker>][ [<Review status>]]
  [<source comment line>]          (the original comment text, right-trimmed, if a source comment applies)
  <source line, tabs replaced by two spaces, with its newline>
  <spaces up to the column>^        (one space per character before the column, plus one extra per tab)
  [details with --print-steps]
  <empty line>
  ```

  `[<Review status>]` is printed when the status is not `unreviewed` and the
  main file has not changed; the text is the status with `_` → space,
  capitalized first letter (`False positive`, `Intentional`, `Confirmed`,
  `Suppress`). If the main file changed, the source line, marker and comment
  are replaced by
  `WARNING: source file content is changed or missing. Please re-analyze your project to update the reports.`
  If the source line is empty, nothing is printed for it.
- then `Found <n> defect(s) in <base name>` and an empty line; or, for a
  path with no reports that has not been printed before,
  `Found no defects in <base name>`.

`--print-steps` details:

```
  Report hash: <hash>
  Macro expansions:                       (if any)
    <idx>, <file name>:<line>:<col>: Macro '<name>' expanded to '<expansion>'
  Notes:                                  (if any)
    <idx>, <file name>:<line>:<col>: <message>
  Steps:
    <idx>, <file name>:<line>:<col>: <message>
```

`<idx>` is right-aligned to the width of the list length's digit count
(format `'    %<w>d, '`), `<file name>` is the base name.

### 4.3 Statistics tables

Printed with the table renderer
([common-infrastructure.md](common-infrastructure.md) §7), each section only
if non-empty, in insertion (first-seen) order:

```
\n----==== Severity Statistics ====----\n
<table: Severity | Number of reports>
\n----=================----\n
\n----==== Checker Statistics ====----\n
<table: Checker name | Severity | Number of reports>
...
\n----==== File Statistics ====----\n
<table: File name (base name) | Number of reports>
...
\n----======== Summary ========----\n
<table without header, footer style: "Number of processed analyzer result files" | n ; "Number of analyzer reports" | n>
\n----=================----\n
```

Note: "File Statistics" counts by original path but displays the base name,
so two files with the same name appear as two rows with the same label.

## 5. Review status of a report

See [review-status-and-suppression.md](review-status-and-suppression.md):
source comment → `review_status.yaml` rule → unreviewed.

## 6. Suppress files

Line formats (each line stripped; tried in this order):

| Format | Regex | Status |
|---|---|---|
| source-suppress | `^(?P<hash>[\d\w]{32})(#(?P<type>\d))?\s*\|\|\s*(?P<file>[^\\\|]+)\s*\|\|\s*(?P<comment>[^\|]*)\s*\|\|\s*(?P<status>[<chars>]*)$` | the status text |
| new | same without the last field | `false_positive` |
| old | `^(?P<hash>[\d\w]{32})(#\d)?\s*\|\|\s*(?P<comment>[^\|]*)$` | `false_positive`, file name empty |

`<chars>` is a **character class** made of the letters of the four marker
names, so any word composed of those letters (e.g. `false_positive`,
`intentional`, `confirmed`) matches. Non-matching, non-empty lines → warning
`Malformed suppress line: <line>`.

A report is suppressed if some entry has the same hash **and** the same file
base name **and** the entry's status is **not** in the review-status filter
(so with the default filter, `confirmed` entries do not hide reports).
Old-format entries (empty file name) never match.

`--export-source-suppress` writing: for each kept report with a non-unreviewed
review status, if no entry with the same hash and (same file name or empty
file name) exists, append `<hash>||<file base name>||<message>||<status>\n`.
The status written is the report's review status (`false_positive`,
`intentional`, `confirmed`; `suppress` comments are already mapped to
`false_positive`).
