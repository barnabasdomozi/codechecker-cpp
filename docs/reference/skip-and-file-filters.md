# Reference: skipfiles and file filters

Usage: [../usage/skip-and-file-filters.md](../usage/skip-and-file-filters.md)

Python source: `codechecker_common/skiplist_handler.py`,
`analyzer/codechecker_analyzer/cli/analyze.py` (`__get_skip_handlers`,
`get_affected_file_paths`), `buildlog/log_parser.py`, `analysis_manager.py`,
`cli/parse.py`, `tools/tu_collector`.

## 1. Skipfile format and matching

1. Split the content into lines; strip each; drop empty lines and lines
   starting with `#`.
2. A valid line has length ≥ 2 and starts with `-` or `+`. Invalid lines →
   warning `Skipping malformed skipfile pattern: <line>`, ignored.
3. Pattern = `normpath(line[1:].strip())` (removes trailing `/`, collapses
   `//` and `.` segments).
4. Convert the pattern from shell-glob syntax to a regex
   (`*` → any characters **including** `/`, `?` → one character, `[...]` →
   character class), remove the end anchor, and append `(?:/.*)?$`. So a
   pattern also matches everything below it when it names a directory.
5. `should_skip(path)`: try the rules in file order; the first rule whose
   regex matches the path **from the start** decides: `-` → skip, `+` → keep.
   No match → keep.

A list of handlers skips a path if **any** handler skips it.

Paths given to the matcher are absolute source paths (build actions) or
report file paths (original, untrimmed).

## 2. Where skipping applies in `analyze`

Skip handlers = (a) the `--file` handler if `--file` is given, then (b) the
`-i` skipfile handler.

| Stage | Behavior |
|---|---|
| Compilation database parsing | An action whose source is skipped is dropped (counted as skipped) unless CTU or statistics is enabled; with CTU/statistics it is dropped only if the pre-analysis handlers also skip it (they are the same handlers only for `--ctu-collect` alone or `--stats-collect`). |
| Pre-analysis (CTU collect, statistics) | Uses the skip handlers only for `--ctu-collect` alone or `--stats-collect`; otherwise every file is pre-analyzed. |
| Workers | Jobs whose source is skipped are removed before analysis (logged at debug level). |
| Result handling | With `--drop-reports-from-skipped-files`, reports whose **main file** is skipped are removed from the results of every analyzed file. Without it, reports in skipped headers are kept. |

`analyze` copies the skipfile to `<out>/skip_file` and `<out>/conf/`.

## 3. `--file`

For each `--file` argument `f`:
- If `f` contains no `*`, it is converted to an absolute, canonical path
  (resolved relative to the current directory); otherwise kept as is.
- If `f` (as written) exists **and** ends with a header extension
  (`.h .hh .H .hp .hxx .hpp .HPP .h++ .tcc`), it is also collected as a header.

If headers were collected: INFO `Get dependent source files for '<headers>'...`,
compute the source files that include them (see
[tu-collector.md](tu-collector.md) "dependents"), INFO `... done.`, and add
those sources to the list.

The `--file` handler is a skip list built from:

```
+<f1>
+<f2>
...
+/*.h
+/*.H
+/*.tcc
-*
```

So only the given files (and all `.h`, `.H`, `.tcc` headers) are kept.

Quirks (verified):
- The header is compared with the dependency data using the path **as
  written**, while dependencies are absolute paths. A relative header path is
  therefore recognized but finds no dependent sources; only absolute paths
  work.
- A glob (`*/util.h`) does not exist as a file, so it is not treated as a
  header.

## 4. `parse -i` and `parse --file`

`parse` builds a skip list from `-i` and keeps a report only if its main file
is not skipped. `parse --file P...` keeps reports whose main file matches any
pattern (`*` wildcard, see [parse.md](parse.md)). These do not change the
result files.

## 5. `check`

`check` passes `-i`, `--file` and `--drop-reports-from-skipped-files` to its
analyze step.
