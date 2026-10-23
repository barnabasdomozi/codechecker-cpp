# Reference: `CodeChecker cmd diff` (shared logic and local folders)

Usage: [../usage/local-diff.md](../usage/local-diff.md)

Python source: `web/client/codechecker_client/cli/cmd.py` (`__register_diff`,
`__add_filtering_arguments`, `__add_output_formats`),
`web/client/codechecker_client/cmd_line_client.py` (`handle_diff_results`,
`filter_local_file_remote_run`, `get_diff_local_dirs`,
`get_report_dir_results`, `skip_report_dir_result`,
`parse_report_filter_offline`, `print_reports`),
`filter_defaults.py`.

This page describes the command line, the classification of the two sides,
the local-only comparison and the output stage, which all four comparison
modes share. The modes that involve a server run are in
[cmd-diff.md](cmd-diff.md).

## 1. Options

```
CodeChecker cmd diff -b BASE... -n NEW... (--new | --resolved | --unresolved)
                     [filter options] [--filter-preset NAME] [--print-steps]
                     [-o FORMAT...] [-e DIR] [-c] [--url PRODUCT_URL] [--verbose L]
```

| Option | Value | Default |
|---|---|---|
| `-b/--basename` | ≥ 1 strings | required |
| `-n/--newname` | ≥ 1 strings | required |
| `--new`, `--resolved`, `--unresolved` | flag | exactly one is required (argparse mutually exclusive group) |
| `--print-steps` | flag | |
| `-o/--output` | ≥ 1 of `plaintext rows table csv json html gerrit codeclimate` | `plaintext` (a list) |
| `-e/--export-dir DIR` | | |
| `-c/--clean` | flag | |
| `--url` | `[http[s]://]host:port/Endpoint` | `localhost:8001/Default` |
| `--filter-preset NAME` | | |

Filter options (shared with `cmd results`, see
[cmd-results.md](cmd-results.md) §2). Defaults that are present even when
the option is not given:

| Option | Default |
|---|---|
| `--review-status` | `unreviewed confirmed` |
| `--detection-status` | `new reopened unresolved` |
| `--uniqueing` | `off` |
| `--anywhere-on-report-path` | false |
| `--single-origin-report` | false |

All other filter options are absent unless given.

`cmd.py` also has a check that `-o html` requires `-e`, but it compares the
`-o` **list** with the string `"html"`, so it never triggers; the check in
§2 does the job instead.

## 2. Pre-checks (in this order)

The logger is always initialised to write to **stderr** (the code intends
"stderr unless the format is `table`", but compares the list with a string).

1. More than one `-o` value without `-e` →
   `Export directory is required if multiple output formats are selected!`,
   exit 1.
2. `html` in `-o` without `-e` →
   `Argument --export not allowed without argument --output when exporting to HTML.`
   (wording copied from `parse`), exit 1.
3. `gerrit` in `-o`: `CC_REPO_DIR` and `CC_CHANGED_FILES` must be set (same
   check and messages as `parse -e gerrit`,
   [parse-export.md](parse-export.md) "Gerrit"), else exit 1.
4. Deprecation warnings for `--detected-at` / `--fixed-at`:
   `"--detected-at" option has been deprecated. Use --detected-before/--detected-after options to filter the results by detection date. For more information see the help.`
   (and the same for `--fixed-at` with `--fixed-before/--fixed-after` and "fix
   date").
5. `-c`: if `-e DIR` is an existing directory, print (stdout)
   `Previous analysis results in '<DIR>' have been removed, overwriting with current results.`
   and delete it recursively. `-c` **without** `-e` crashes (`TypeError`,
   exit 1).
6. `-e DIR` that does not exist is created (with parents).

## 3. Classifying each side

Each value of `-b` and of `-n` is classified independently, in this order:

1. an existing **directory** → local report directory (made absolute);
2. an existing **file** that is a baseline file → baseline file (made
   absolute). "Baseline file" means the file name ends with `.baseline`
   (see [parse-export.md](parse-export.md) "Baseline" for the format);
3. anything else → a run name (pattern) on the server.

A non-existing path is therefore silently treated as a run name.

Consistency rules (all errors are logged, then exit 1 if any):
- base side: (local dirs or baseline files) **and** run names →
  `All base run names must have the same type: local directory (<dirs>) / baseline files (<files>) or run names (<runs>).`
- new side: local dirs **and** run names → the same message with "new run
  names". Note: a new side with baseline files **and** run names passes this
  check; the run names are then ignored (the comparison below is local-only).

INFO lines (each only if non-empty, lists joined with `, `):

```
Matching local report directories (--baseline): <dirs>
Matching local baseline files (--baseline): <files>
Matching local report directories (--newname): <dirs>
Matching local baseline files (--newname): <files>
```

If either side has run names, also `Given remote runs (--baseline): ...` /
`Given remote runs (--newname): ...`, and a server client is created for
`--url` ([server-api.md](server-api.md) "Client"). If that fails:
`Failed to get remote runs from server! Please make sure that CodeChecker server is running on host '<url>' or these are valid directory paths!`
and the original exit code.

If `--component` is given, every component name must exist on the server
(`getSourceComponents([name])` non-empty), else
`The following source components don't exist: <names>`, exit 1. Without a
server client (local-only) this crashes (`AttributeError`, exit 1).

## 4. Choosing the mode

| Base side | New side | Mode |
|---|---|---|
| dirs / baselines | dirs / baselines | local–local (§5) |
| run names | dirs / baselines | remote–local ([cmd-diff.md](cmd-diff.md) §3) |
| dirs / baselines | run names | local–remote ([cmd-diff.md](cmd-diff.md) §4) |
| run names | run names | remote–remote ([cmd-diff.md](cmd-diff.md) §2) |

The diff type is `NEW`, `UNRESOLVED` or `RESOLVED` from the flag.

## 5. Local–local comparison

### 5.1 Report filter

Built **without** a server (`parse_report_filter_offline`). The
`--filter-preset` and `--tag` options are ignored in this mode.

Values of `--severity`, `--detection-status`, `--review-status` are upper-cased
and looked up in the API enums (`Severity`: UNSPECIFIED STYLE LOW MEDIUM HIGH
CRITICAL; `DetectionStatus`: NEW RESOLVED UNRESOLVED REOPENED OFF
UNAVAILABLE; `ReviewStatus`: UNREVIEWED CONFIRMED FALSE_POSITIVE
INTENTIONAL). An unknown value raises `KeyError` (traceback, exit 1); e.g.
`--review-status suppress` crashes. (A later "Invalid ... value(s)" check
exists but is unreachable.)

Only these filters are applied to local reports:

| Filter | Rule |
|---|---|
| `--review-status` | §5.3 |
| `--severity` | the checker's severity label ([checker-labels.md](checker-labels.md)), compared case-insensitively with the given names |
| `--checker-name P...` | any `P` matches the checker name |
| `--file P...` | any `P` matches the report's main file path (absolute) |
| `--checker-msg P...` | any `P` matches the message |

Pattern matching: the regex `'^' + P.replace('*', '.*') + '$'`,
case-insensitive. Other regex metacharacters in `P` are **not** escaped (a
`.` matches any character). Because the whole path must match, `--file main.c`
matches nothing; use `--file '*/main.c'`.

All other filters (`--report-hash`, `--analyzer-name`, `--bug-path-length`,
`--detection-status`, `--report-status`, `--uniqueing`, date filters,
`--anywhere-on-report-path`, `--single-origin-report`) are parsed (date
strings converted, warnings for `--anywhere-on-report-path` without
`--file`/`--component` and `--single-origin-report` without `--component`
are logged) but **have no effect**.

Timestamp options take `year:month:day[:hour[:minute[:second]]]` (missing
parts are 0), interpreted in local time and converted to a Unix timestamp.
An invalid date is an argparse error (exit 1, see
[common-infrastructure.md](common-infrastructure.md) §4).

### 5.2 Reading one side

For the directories of one side (`get_report_dir_results`):

1. Walk all result files (`.plist` / `.sarif`) of all directories, grouped by
   directory ([parse.md](parse.md) §4 step 5).
2. Per directory: a **new** review status handler; if
   `<dir>/review_status.yaml` is a file, load it (format error → ERROR, exit 1).
3. Per result file: read the reports with checker labels
   ([report-format.md](report-format.md)); set each report's review status
   ([review-status-and-suppression.md](review-status-and-suppression.md):
   source comment → rule → `unreviewed`). An error (ambiguous comments) is
   logged and the remaining reports of the file keep their default status.
4. Drop reports whose **path hash** was already seen on this side
   ([report-hash.md](report-hash.md)); the seen-set spans all files of the
   side.
5. Drop reports rejected by the §5.1 filters.

Baseline files contribute only report hashes (one per line, see
[parse-export.md](parse-export.md) "Baseline"); they produce no printable
reports.

### 5.3 Review status selection

`statuses` = the `--review-status` values (lower-case enum names) plus
`unreviewed`. With the default this is `unreviewed confirmed`. Only reports
whose review status is in `statuses` are kept, on **both** sides. So
`false_positive`, `intentional` and `suppress` reports are removed from both
sides by default, i.e. they behave as "not reported" (and appear as resolved
if they were reported normally in the base).

### 5.4 Set operations

```
base_hashes = {hash of kept base reports} ∪ hashes of base baseline files
new_hashes  = {hash of kept new reports}  ∪ hashes of new baseline files

NEW:        reports  = [r in new_reports  if r.hash ∉ base_hashes]
            leftover = new_hashes  − {hashes of all kept new reports}
UNRESOLVED: reports  = [r in new_reports  if r.hash ∈ base_hashes]
            leftover = new_hashes  − {hashes of all kept new reports}
RESOLVED:   reports  = [r in base_reports if r.hash ∉ new_hashes]
            leftover = base_hashes − {hashes of all kept base reports}
```

`reports` keeps the reading order. `leftover` = hashes that come only from
baseline files on the printed side; they are reported in §6 and make the exit
code 2 even if `reports` is empty. Consequences:
- base = folder, new = baseline, `--new`: `reports` is empty, but `leftover`
  is every hash of the baseline file → exit 2 (known issue).
- base = baseline, new = folder, `--resolved` or `--unresolved` with a
  baseline on the printed side: `leftover` is **every** hash of the baseline
  file, including those also present on the other side (the leftover set is
  not intersected with the difference).

After printing: INFO
`Compared the following local files / directories: <base dirs+files> and <new dirs+files>`.

## 6. Output stage (`print_reports`, all modes)

Inputs: the report list, the leftover hash set (none in remote–remote mode),
`--print-steps`, `-e DIR`, `-o` formats.

1. Leftover hashes non-empty → INFO
   `Couldn't get local reports for the following baseline report hashes: <sorted, joined with ", ">`.
2. For each report: add it to the statistics; collect its changed files; if
   `CC_REPO_DIR` is set, trim that prefix from all paths of the report (for
   every format).
3. For each format, in the order given:

| Format | Output |
|---|---|
| `plaintext` | reports grouped by file and printed like `parse` ([parse.md](parse.md) §4.2) to stdout; a file appears only if it has reports |
| `html` | one page per **source file** (not per result file as in `parse`): `DIR/<base name>_ <h>.html` — note the space — where `h = int(md5_hex(path), 16) mod 10^8` in decimal; INFO `Generating HTML output files to file://<DIR> directory:` before the pages. Page content as in [parse-export.md](parse-export.md) "HTML" |
| `rows`, `table`, `csv` | header `File, Checker, Severity, Msg, Source`; one row per report whose source line is known: `(severity, "<path>:<line>:<column>", message, checker, source line right-trimmed)`. The header labels do not match the columns (known issue). Rendered with the two-dimensional formatter ([common-infrastructure.md](common-infrastructure.md) §7) and printed to stdout |
| `json` | `{"version": 1, "reports": [<report JSON>]}` ([parse-export.md](parse-export.md) "JSON") → `DIR/reports.json` if `-e`, else stdout |
| `gerrit` | review JSON ([parse-export.md](parse-export.md) "Gerrit") → `DIR/gerrit_review.json` or stdout |
| `codeclimate` | issue list ([parse-export.md](parse-export.md) "Code Climate") → `DIR/codeclimate_issues.json` or stdout |

   JSON files are written compactly (`json.dumps` defaults, `", "` and
   `": "` separators); writing a file logs INFO
   `JSON report file was created: <path>`; stdout output ends with a newline.
4. If `html` was selected: write `index.html` and `statistics.html` and print
   the browser hints, as in `parse` ([parse-export.md](parse-export.md)).
5. If `plaintext` was selected: print the statistics tables
   ([parse.md](parse.md) §4.3). "Number of processed analyzer result files"
   is always 0 here.
6. If any changed files were collected: the changed-files WARNING
   ([parse.md](parse.md) §4 step 8).

## 7. Exit code

| Code | Condition |
|---|---|
| 0 | the report list and the leftover hash set are both empty |
| 1 | errors above, server errors ([server-api.md](server-api.md) "Client") |
| 2 | at least one report, or at least one leftover baseline hash |
| 1 | also argparse errors ([common-infrastructure.md](common-infrastructure.md)) |
