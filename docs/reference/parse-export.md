# Reference: export formats (`parse -e`, also used by `cmd diff`)

Usage: [../usage/parse-export.md](../usage/parse-export.md)

Python source:
`tools/report-converter/codechecker_report_converter/report/output/`
(`json.py`, `codeclimate.py`, `gerrit.py`, `baseline.py`, `sarif.py`,
`html/html.py`, `html/static/`), `report/parser/sarif.py`,
`util.py` (`dump_json_output`), `analyzer/codechecker_analyzer/cli/parse.py`.

General rules for `parse -e X`:
- The kept reports (after all filters, with trimmed paths) are converted.
- JSON-based formats are serialized with Python `json.dumps` defaults
  (`", "` and `": "` separators, non-ASCII escaped as `\uXXXX`) on one line.
  With an output file: written (overwritten), INFO
  `JSON report file was created: <path>`. Without: printed to stdout followed
  by a newline. `-o DIR` (a directory that does not exist yet) is used as a
  file path; for these formats there is no default file name in practice.
- Exit code 2 if any report was exported.

## JSON (`-e json`)

```json
{"version": 1, "reports": [<report>, ...]}
```

`<report>`:

```json
{"analyzer_result_file_path": "...", "file": {"id": "...", "path": "...", "original_path": "..."},
 "line": n, "column": n, "message": "...", "checker_name": "...", "severity": "HIGH"|null,
 "report_hash": "...", "analyzer_name": "..."|null, "category": "..."|null, "type": "..."|null,
 "review_status": "unreviewed"|"confirmed"|"false_positive"|"intentional"|"suppress"|"",
 "bug_path_events": [{"file": <file>, "line": n, "column": n, "message": "...", "range": <range>|null}],
 "bug_path_positions": [{"range": <range>|null, "file": <file>}],
 "notes": [<event>], "macro_expansions": [{"name": "...", <event fields>}],
 "annotations": {}}
```

`<range>` = `{"start_line", "start_col", "end_line", "end_col"}`. `path` is the
trimmed path, `original_path` the untrimmed one.

## Code Climate (`-e codeclimate`)

A JSON **array**; per report:

```json
{"type": "issue", "check_name": "<checker>", "description": "<message>",
 "categories": ["Bug Risk"], "fingerprint": "<report hash>",
 "severity": "<mapped>", "location": {"path": "<file path>", "lines": {"begin": <line>}}}
```

Severity map: `CRITICAL→critical`, `HIGH→major`, `MEDIUM→minor`,
`LOW→minor`, `STYLE→info`, `UNSPECIFIED→info`, missing → `info`.

## Gerrit (`-e gerrit`)

Requires `CC_REPO_DIR` and `CC_CHANGED_FILES` to be set; each missing one
gives an error before parsing:
- `When using gerrit output the 'CC_REPO_DIR' environment variable needs to be set to the root directory of the sources, i.e. the directory where the repository was cloned!`
- `When using gerrit output the 'CC_CHANGED_FILES' environment variable needs to be set to the path of changed files json from Gerrit!`

Changed files: read `CC_CHANGED_FILES` (if it exists); find the first
`{...}` block (regex `\{[\s\S]*\}`, greedy, tolerating leading garbage);
parse it as JSON; the keys, except those containing `/COMMIT_MSG`, are the
changed files.

Per report (`file_name` = report file path, trimmed):
- `rel` = `relpath(file_name, CC_REPO_DIR)` if the repo dir is set and
  `file_name` is absolute with a directory part; else `file_name`;
- message = `[<severity>] <rel>:<line>:<col>: <message> [<checker>]\n<source line>`
  (the source line keeps its newline);
- if `CC_CHANGED_FILES` is set and `file_name` does not **end with** any
  changed file name → collect the message in an "unchanged files" list;
- else add `{"range": {"start_line": L, "start_character": C, "end_line": L, "end_character": C}, "message": msg}`
  to `comments[rel]`.

Result:

```json
{"tag": "jenkins",
 "message": "CodeChecker found <N> issue(s) in the code.<extra>",
 "labels": {"Code-Review": -1 or 1, "Verified": -1 or 1},
 "comments": {"<rel>": [...]}}
```

`N` counts **all** reports (also those in unchanged files). Labels are `-1`
if `N > 0`, else `1`. `<extra>`: if there were reports in unchanged files,
`"\n\nThere following reports are introduced in files which are not changed and can't be shown as individual reports:\n" + msgs joined by "\n" + "\n"`;
then, if `CC_REPORT_URL` is set, `" See: <url>"`.

## Baseline (`-e baseline`)

- Content: sorted unique non-null report hashes.
- `-o` must end with `.baseline`. Without `-o` nothing is written.
- Writing **merges** with an existing file: read existing hashes (one per
  line, stripped); if no new hash → INFO `Baseline file (<f>) is up-to-date.`
  and keep the file; else INFO `Merging existing baseline file: <f>` or
  `Creating new baseline file: <f>`, INFO totals of old/new hashes, and
  rewrite the file with the sorted union joined by `\n` (no trailing newline).
- Reading (used by `cmd diff`): each line stripped; the union of all given
  files.

## SARIF (`-e sarif`)

Default review-status filter for SARIF: all statuses.

```json
{"version": "2.1.0",
 "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json",
 "runs": [{
   "tool": {"driver": {"name": "CodeChecker", "version": "6.30.0", "rules": [<rule>...]}},
   "results": [<result>...]}]}
```

- `name`/`version`: `CodeChecker` and `major.minor.revision` from
  `analyzer_version.json` when running inside CodeChecker; else
  report-converter's own title and version.
- `<rule>` (one per checker, first occurrence):
  `{"id": checker, "fullDescription": {"text": <first report's message>},
  "helpUri": <first doc_url label>, "defaultConfiguration": {"level": L}}`
  where `L` = `error` (HIGH, CRITICAL), `warning` (MEDIUM, UNSPECIFIED),
  `note` (STYLE, LOW). `helpUri`/`defaultConfiguration` only when known.
- `<result>`:

  ```json
  {"ruleId": checker, "message": {"text": message},
   "locations": [{"physicalLocation": {"artifactLocation": {"uri": "file://<path>"},
                                        "region": {"startLine": L, "startColumn": C}}}],
   "suppressions": [{"kind": "inSource"|"external", "status": "accepted"|"rejected",
                     "justification": "<review message>"}],          // only if not unreviewed
   "codeFlows": [{"threadFlows": [{"locations": [<tfl>...]}]}]}      // only if any locations
  ```

  `status`: `accepted` for suppress/false_positive/intentional, `rejected`
  for confirmed. Thread flow locations, in this order: bug path events
  (`"importance": "important"`), notes and macro expansions
  (`"importance": "essential"`), each with `location.message.text`; then bug
  path positions (no importance, no message). Region: full range
  (`startLine/startColumn/endLine/endColumn`) when the item has a range, else
  `startLine/startColumn`.
- The report hash is **not** written. Reading SARIF back recomputes it
  (path-sensitive).

### Reading SARIF (`parse`, `store`, `report-converter -t gcc`)

1. If the JSON has no `runs` (or empty) → no reports.
2. For each run: rules from `tool.driver.rules` (`id → rule`); analyzer
   name = `tool.driver.name` (default `unknown`); `originalUriBaseIds`.
3. For each result: `ruleId` is the checker; message = `message.text`, or
   `rule.messageStrings[message.id].text` formatted with `message.arguments`
   (`{0}`, `{1}` placeholders).
4. Events: for each `codeFlows[].threadFlows[].locations[].location` that
   has a `message`: event at its physical location.
5. For each entry of `locations`: physical location → file and range; skip
   if missing. Region: `startLine` required; `startColumn` default 1;
   `endLine` default start line; `endColumn` default start column.
   File: `artifactLocation.uri`, prefixed by the resolved `uriBaseId` chain
   (`originalUriBaseIds[id].uri`, recursively through its `uri_base_id`);
   missing base ids → errors (`Missing 'originalUriBaseIds' ...`) and the
   whole file yields no reports. The URI is parsed; path = `netloc + path`
   (so `file:///a/b` → `/a/b`).
6. One report per location, with the events (or the default single event),
   severity from labels, path-sensitive hash.

## HTML (`-e html -o DIR`)

For each result file with kept reports (files without reports: INFO
`No report data in <file> file.`), print `Parsing input file '<file>'.` and:
- if any file of these reports changed since analysis → no page for this
  result file;
- else write `DIR/<result file base name>.html`: the `layout.html` template
  with `$`-placeholders substituted by the inlined contents of the static
  CSS/JS files (`style.css`, `buglist.css`, `bugview.css`, `statistics.css`,
  `icon.css`, `table.css`, CodeMirror license/CSS/JS, `clike.min.js`,
  `bugviewer.js`, `buglist.js`, `browsersupport.js`) and `$report_data` =
  JSON `{"files": {<id>: {"id", "filePath", "content" (HTML-escaped source)}},
  "reports": [<html report>]}`. INFO `Html file was generated: <path>`.

`<html report>`: `{fileId, reportHash, path, checker: {name, url}, analyzerName,
line, column, message, events: [{message, fileId, line, column}],
macros: [{message, name, fileId, line, column}], notes: [...],
reviewStatus: "<Formatted status>", severity, testcase, timestamp,
chronologicalOrder}` (the last three from annotations `testcase`,
`timestamp`, `chronological_order`). Severity comes from the labels
(`UNSPECIFIED` without labels); `url` is the first `doc_url` label or `""`.

At the end:
- `DIR/index.html`: `index.html` template with `$table_reports` = JSON list of
  `{link (page base name), file-path, report-hash, checker-name, checker-url,
  line, message, review-status, severity, bug-path-length (number of events),
  testcase, timestamp, chronological-order}`;
- `DIR/statistics.html`: `statistics.html` template with
  `$num_of_analyzer_result_files`, `$number_of_reports`,
  `$checker_statistics` (HTML rows, checkers sorted by name: name, severity
  icon, count) and `$severity_statistics` (rows in the order CRITICAL, HIGH,
  MEDIUM, LOW, STYLE, UNSPECIFIED);
- the normal statistics tables are printed, then

  ```

  To view statistics in a browser run:
  > firefox <DIR>/statistics.html

  To view the results in a browser run:
  > firefox <DIR>/index.html
  ```

The static files (templates, CSS, JS, CodeMirror) are part of the package
and can be reused as is by a port.
