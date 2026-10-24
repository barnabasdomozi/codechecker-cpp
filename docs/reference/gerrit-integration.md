# Reference: Gerrit integration

Usage: [../usage/gerrit-integration.md](../usage/gerrit-integration.md)

Python source: `scripts/gerrit_changed_files_to_skipfile.py` (installed as
`bin/gerrit_changed_files_to_skipfile.py`),
`tools/report-converter/codechecker_report_converter/report/output/gerrit.py`.

Two independent pieces:

1. the Gerrit review JSON output of `CodeChecker parse -e gerrit` and
   `CodeChecker cmd diff -o gerrit` — specified in
   [parse-export.md](parse-export.md) "Gerrit (`-e gerrit`)" (environment
   `CC_REPO_DIR`, `CC_CHANGED_FILES`, `CC_REPORT_URL`), and for `cmd diff`
   in [local-diff.md](local-diff.md) / [cmd-diff.md](cmd-diff.md);
2. the changed-files → skipfile script (§1).

## 1. `gerrit_changed_files_to_skipfile.py`

```
gerrit_changed_files_to_skipfile.py FILES_CHANGED [SKIPFILE]
```

Plain `argparse`; both arguments are opened by argparse (`FILES_CHANGED`
for reading, `SKIPFILE` — default `./skipfile` — for writing, truncating
it). An unreadable input or unwritable output is an argparse error
(exit 2).

Algorithm:
1. For every line of `FILES_CHANGED` that matches `^\{.*\}` (a whole JSON
   object **on one line**; Gerrit's `)]}'` prefix line is ignored):
   parse the line as JSON and, for each key not containing `/COMMIT_MSG`
   (object key order), write `+*/<key>\n`.
2. Finally write `-*\n`.

So the skipfile includes exactly the changed files (matched anywhere by
the `*/` prefix, [skip-and-file-filters.md](skip-and-file-filters.md) §1)
and skips everything else. Exit 0.

⚠️ A pretty-printed (multi-line) JSON matches no line, so the result is
just `-*` — every file is skipped — without any warning (verified). A
line that matches the regex but is not valid JSON crashes
(`json.JSONDecodeError`, exit 1). A port should parse the whole file the
way the gerrit output does (first `{...}` block of the file,
[parse-export.md](parse-export.md)).
