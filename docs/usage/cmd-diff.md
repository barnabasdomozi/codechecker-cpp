# Compare runs on a server (`cmd diff`)

`cmd diff` compares a base and a new side and prints the reports that are new,
resolved, or present in both. Each side can be:
- one or more **server runs** (names with `*` wildcards),
- a run at a specific **tag**: `run_name:tag_name` (escape a literal colon as
  `run\:name`),
- one or more **local report folders**,
- **baseline files**.

Local-only comparisons, output formats and exit codes are described in
[local-diff.md](local-diff.md). This page covers server runs.

```
CodeChecker cmd diff -b BASE [...] -n NEW [...] (--new | --resolved | --unresolved)
                     [filters] [--filter-preset NAME] [--print-steps]
                     [-o FORMAT ...] [-e EXPORT_DIR] [-c] [--url PRODUCT_URL]
```

## Compare two tags of a run ✅

```sh
CodeChecker cmd diff -b demo:v1 -n demo:v2 --new --url localhost:8555/Default
```

Prints the 3 reports added between the `v1` and `v2` stores, then a summary.
Exit code `2`.

## Compare a server run with a local folder ✅

```sh
CodeChecker cmd diff -b demo -n ./reports2 --unresolved --url localhost:8555/Default
```

5 reports are in both. This is the usual CI pattern: store the main branch as
a run, analyze a change locally, and diff with `--new` to see only what the
change introduced.

## Compare two different runs ✅

```sh
CodeChecker cmd diff -b multi -n demo --resolved --report-status outstanding --url localhost:8555/Default
```

`--report-status`, `--detection-status`, `--tag` and the date filters apply
when at least one side is a server run. All other filters of
[`cmd results`](cmd-results.md) work too.

## Fix only the new reports ✅ / ⚠️

```sh
CodeChecker cmd diff -b demo2 -n ./fx2 -o json --new --url localhost:8555/Default \
  | CodeChecker fixit ./fx2
```

✅ Works (server run vs. local folder). ⚠️ The JSON of a server-run vs.
server-run diff (`-b demo:v1 -n demo:v2`) makes `fixit` crash with
`KeyError: 'checkedFile'`. See [fixit.md](fixit.md).

## Gerrit output for a server diff

```sh
CC_REPO_DIR=. CC_CHANGED_FILES=changed.json CC_REPORT_URL=http://ci/run/1 \
  CodeChecker cmd diff -b main_branch -n ./reports --new -o gerrit -e ./out --url ...
```

Writes `./out/gerrit_review.json` (see [gerrit-integration.md](gerrit-integration.md)).
