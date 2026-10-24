# Export results to other formats (`parse -e`)

`CodeChecker parse -e FORMAT [-o OUTPUT]` writes the reports in another
format. Formats: `html`, `json`, `codeclimate`, `gerrit`, `baseline`,
`sarif`. The filter options of `parse` (`--file`, `-i`, `--review-status`,
`--trim-path-prefix`, `--suppress`) also apply to exports. The exit code is
`2` when reports are exported.

## HTML ✅

```sh
CodeChecker parse ./reports -e html -o ./html
```

```
./html/
├── index.html                                          # list of reports
├── statistics.html                                     # tables like the terminal summary
├── main.c_clangsa_681db965dea6200719ba62bde2ba8cb4.plist.html
├── main.c_clang-tidy_681db965dea6200719ba62bde2ba8cb4.plist.html
├── main.c_cppcheck_681db965dea6200719ba62bde2ba8cb4.plist.html
└── style.cpp_cppcheck_3d2af223724183d98c10c4e691ad0f87.plist.html
```

One page per result file that has a shown report.

The pages show the source code with the bug path drawn on it, and need no
server. `-o` is required for HTML:

```
[ERROR] - Argument --export not allowed without argument --output when exporting to HTML.
```
(exit code 1)

## JSON ✅

```sh
CodeChecker parse ./reports -e json -o reports.json
CodeChecker parse ./reports -e json        # print to standard output
```

```json
{"version": 1, "reports": [{"analyzer_result_file_path": "~/demo/reports/main.c_clang-tidy_...plist",
  "file": {"id": "~/demo/main.c", "path": "~/demo/main.c", "original_path": "~/demo/main.c"},
  "line": 21, "column": 7, "message": "unused variable 'unused'",
  "checker_name": "clang-diagnostic-unused-variable", "severity": "MEDIUM",
  "report_hash": "249a121710b6a5cb467697f323de0d5f", "analyzer_name": "clang-tidy",
  "category": "clang", "type": null, "review_status": "unreviewed",
  "bug_path_events": [...], "bug_path_positions": [...], "notes": [], "macro_expansions": [],
  "annotations": {}, ...}, ...]}
```

## Code Climate / GitLab ✅

```sh
CodeChecker parse ./reports -e codeclimate -o codeclimate.json
```

```json
[{"type": "issue", "check_name": "clang-diagnostic-unused-variable",
  "description": "unused variable 'unused'", "categories": ["Bug Risk"],
  "fingerprint": "249a121710b6a5cb467697f323de0d5f", "severity": "minor",
  "location": {"path": "~/demo/main.c", "lines": {"begin": 21}}}, ...]
```

GitLab reads this format for merge request code-quality widgets.

## SARIF ✅

```sh
CodeChecker parse ./reports -e sarif -o results.sarif
```

```json
{"version": "2.1.0",
 "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json",
 "runs": [{"tool": {"driver": {"name": "CodeChecker", "version": "6.30.0", ...}}, "results": [...]}]}
```

The SARIF export includes all review statuses by default.

## Baseline ✅

```sh
CodeChecker parse ./reports -e baseline -o reports.baseline
```

A baseline file is a sorted list of report hashes, one per line:

```
04d39a256ef58c412c5acf3f9280fe2e
09296549497056a329a9ce22dda7d710
249a121710b6a5cb467697f323de0d5f
2e5ec744a6a360431ece6fe858d3f172
3b2c4905253f6d0f7409d271d65dbdd6
990560afd081e416b9c7cc4c772c4cff
```

(The last line has no line break at the end.)

The output file must end in `.baseline`:

```
[ERROR] - Baseline files must have '.baseline' extensions.
```
(exit code 1)

Use the baseline with `cmd diff` to find new reports without a server (see
[local-diff.md](local-diff.md)).

## Gerrit ✅

```sh
export CC_REPO_DIR=$HOME/demo                       # required: repository root
export CC_CHANGED_FILES=changed_files.json          # required: Gerrit's list of changed files
export CC_REPORT_URL=http://ci.example.com/run/42   # optional: link added to the message
CodeChecker parse ./reports -e gerrit -o gerrit_review.json
```

```json
{"tag": "jenkins",
 "message": "CodeChecker found 6 issue(s) in the code.\n\nThere following reports are introduced in files which are not changed and can't be shown as individual reports:\n[HIGH] style.cpp:11:12: Division by zero. [cppcheck-zerodiv]\n  return a / z;\n\n See: http://ci.example.com/run/42",
 "labels": {"Code-Review": -1, "Verified": -1},
 "comments": {"main.c": [
   {"range": {"start_line": 21, "start_character": 7, "end_line": 21, "end_character": 7},
    "message": "[MEDIUM] main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]\n  int unused = 5;\n"},
   ...]}}
```

Here only `main.c` is in the changed-files list. Only reports in changed
files become comments (the five `main.c` reports); paths are relative to
`CC_REPO_DIR`. The count in the message includes every report, and reports
in files that were not changed are listed in the message itself ("There
following reports ..." is the reference's wording). Without such reports,
the message is just `CodeChecker found <N> issue(s) in the code. See: <URL>`. Without `-o`, the JSON is printed to standard output. Both
`CC_REPO_DIR` and `CC_CHANGED_FILES` must be set; each missing one gives an
error, e.g. without `CC_CHANGED_FILES`:

```
[ERROR] - When using gerrit output the 'CC_CHANGED_FILES' environment variable needs to be set to the path of changed files json from Gerrit!
```

See [gerrit-integration.md](gerrit-integration.md) for the changed-files
format.
