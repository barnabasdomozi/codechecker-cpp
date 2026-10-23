# Gerrit integration

CodeChecker can produce Gerrit review comments and limit analysis to the files
changed in a Gerrit change.

## Gerrit's changed-files list

Gerrit's REST API (`/changes/{id}/revisions/{rev}/files`) returns a JSON
object keyed by file path:

```json
{"/COMMIT_MSG": {"status": "A", "lines_inserted": 7, "size_delta": 551, "size": 551},
 "main.c": {"lines_inserted": 5, "lines_deleted": 1, "size_delta": 10, "size": 700}}
```

## Analyze only the changed files ✅

```sh
gerrit_changed_files_to_skipfile.py changed.json skipfile
```

`skipfile`:

```
+*/main.c
-*
```

Pass it to `analyze -i skipfile` (see
[skip-and-file-filters.md](skip-and-file-filters.md)). The second argument
defaults to `./skipfile`. The script is in the `bin/` folder of the package.

## Gerrit review JSON from local results ✅

```sh
export CC_REPO_DIR=$HOME/demo
export CC_CHANGED_FILES=changed.json
export CC_REPORT_URL=http://ci.example.com/job/42     # optional
CodeChecker parse reports -e gerrit -o gerrit_review.json
```

```json
{"tag": "jenkins",
 "message": "CodeChecker found 3 issue(s) in the code. See: http://ci.example.com/job/42",
 "labels": {"Code-Review": -1, "Verified": -1},
 "comments": {"main.c": [
   {"range": {"start_line": 21, "start_character": 7, "end_line": 21, "end_character": 7},
    "message": "[MEDIUM] main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]\n  int unused = 5;\n"},
   ...]}}
```

Post this JSON to Gerrit's "set review" endpoint. Without `CC_REPORT_URL`,
the message is just `CodeChecker found 3 issue(s) in the code.`.
`CC_REPO_DIR` and `CC_CHANGED_FILES` are required.

## Gerrit review JSON for new reports only ✅

```sh
CC_CHANGED_FILES=changed.json CC_REPO_DIR=$HOME/demo2 \
  CodeChecker cmd diff -b reports -n reports2 --new -o gerrit -e ./diff_gerrit
```

Writes `./diff_gerrit/gerrit_review.json`. The base can also be a server run.
See [local-diff.md](local-diff.md) and [cmd-diff.md](cmd-diff.md).
