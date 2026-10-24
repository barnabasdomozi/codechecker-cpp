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
 "message": "CodeChecker found 6 issue(s) in the code.\n\nThere following reports are introduced in files which are not changed and can't be shown as individual reports:\n[HIGH] style.cpp:11:12: Division by zero. [cppcheck-zerodiv]\n  return a / z;\n\n See: http://ci.example.com/job/42",
 "labels": {"Code-Review": -1, "Verified": -1},
 "comments": {"main.c": [
   {"range": {"start_line": 21, "start_character": 7, "end_line": 21, "end_character": 7},
    "message": "[MEDIUM] main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]\n  int unused = 5;\n"},
   ...]}}
```

Post this JSON to Gerrit's "set review" endpoint. Only `main.c` is in
`changed.json`, so its five reports become comments; the count in the message
includes all six reports, and the report in the unchanged `style.cpp` is
listed in the message. Without `CC_REPORT_URL`, the ` See: <URL>` part is
left out. `CC_REPO_DIR` and `CC_CHANGED_FILES` are required.

## Gerrit review JSON for new reports only ✅

```sh
CC_CHANGED_FILES=changed.json CC_REPO_DIR=$HOME/demo2 \
  CodeChecker cmd diff -b reports -n reports2 --new -o gerrit -e ./diff_gerrit
```

Writes `./diff_gerrit/gerrit_review.json`. The base can also be a server run.
With only `main.c` changed, the two new `main.c` reports
(`core.NullDereference`, `cppcheck-nullPointer`) become comments, and the
message counts all four new reports (`CodeChecker found 4 issue(s) in the
code.`) and lists the two new ones in `style.cpp`.
See [local-diff.md](local-diff.md) and [cmd-diff.md](cmd-diff.md).
