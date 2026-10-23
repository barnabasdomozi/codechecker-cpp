# Reference: `CodeChecker fixit`

Usage: [../usage/fixit.md](../usage/fixit.md)

Python source: `analyzer/codechecker_analyzer/cli/fixit.py`,
`analyzers/analyzer_types.py` (`is_ignore_conflict_supported`).

```
CodeChecker fixit FOLDER... [-a/--apply] [-i/--interactive]
                  [--checker-name [PATTERN ...]] [--file [PATTERN ...]]
```

## 1. Inputs

- Fix-it files: every file in `<FOLDER>/fixit/` (written by clang-tidy
  `--export-fixes` during `analyze`, one YAML per action:
  `<src>_clang-tidy_<hash>.yaml`). A folder without `fixit/` → INFO
  `No fixits in <folder>`.
- YAML format (clang-apply-replacements):

  ```yaml
  MainSourceFile: /abs/style.cpp
  Diagnostics:
  - DiagnosticName: modernize-use-nullptr
    DiagnosticMessage:
      Message: use nullptr
      FilePath: /abs/style.cpp
      FileOffset: 49
      Replacements:
      - {FilePath: /abs/style.cpp, Offset: 49, Length: 1, ReplacementText: nullptr}
    Level: Warning
    BuildDirectory: /abs
  ```

  Loaded with all scalars as strings.
- Optional report list from **stdin** when stdin is not a terminal: the whole
  stdin is read and stripped; if non-empty it must be JSON (else
  `JSON format error on standard input: <e>`, exit 1). If the JSON is an
  object with `version`, its `reports` member is used; otherwise the JSON
  itself (a list).

## 2. Pre-checks

1. `clang-apply-replacements` not found ([analyzers.md](analyzers.md) §2) →
   ERROR `clang-apply-replacements tool is not found` and **return with exit
   code 0**.
2. `-i` while stdin is not a terminal →
   `Interactive mode can't be used in case of JSON data arriving from standard input.`,
   exit 1.

## 3. Filtering one YAML file

Pattern lists become one regex: each pattern is regex-escaped, `*` → `.*`,
joined with `|`, then `$` appended (so only the **last** alternative is
anchored at the end), matched from the start. An empty list matches
everything.

A diagnostic is kept if all hold (evaluated in this order, short-circuit):
1. `DiagnosticName` matches the `--checker-name` regex;
2. it has at least one replacement;
3. `DiagnosticMessage.FilePath` matches the `--file` regex;
4. report match (only if a report list was given): convert
   `FileOffset` to (row, col) by walking the file's lines (UTF-8, undecodable
   bytes dropped; col = remaining offset + 1); keep if some report has
   `line == row`, `column == col` and `checkedFile == FilePath`. Reports
   without `checkedFile` raise `KeyError` (crash, exit 1). The server report
   format (`cmd results -o json`, a list of `ReportData`) has `checkedFile`;
   the local report format written by `cmd diff -o json`
   ([parse-export.md](parse-export.md) "JSON") does not, so every non-empty
   diff crashes. An empty list disables this filter (all fixes pass);
5. the file exists (else collected as "not existing");
6. the file's mtime is not newer than the YAML file's mtime (else collected
   as "modified").

With `-i`, each kept diagnostic is printed (YAML dump) followed by the prompt
`y/<Enter> - Yes\nOther     - No\nCtrl-C    - Cancel\nApply: `; it is kept if
the answer is `Y`, `y` or empty.

The files of the remaining diagnostics are collected as "updated".

## 4. Listing (no `-a`, no `-i`)

For each YAML file: filter it; if diagnostics remain, print the filtered
document (YAML dump, block style) to stdout.

## 5. Applying (`-a` or `-i`)

1. Check whether `clang-apply-replacements --help` mentions
   `--ignore-insert-conflict`.
2. Per input folder, in a new temporary directory:
   - for each YAML file: filter; if diagnostics remain, write the filtered
     YAML into the temp dir under the same name;
   - without the ignore flag: run
     `clang-apply-replacements <tempdir>` after **each** YAML file and delete
     that file (so one failing file does not block the others);
   - with the ignore flag: run
     `clang-apply-replacements --ignore-insert-conflict <tempdir>` once at the
     end.
   The tool's own output is passed through.

## 6. Summary (both modes, on stderr)

```
Updated files:
<sorted list>
Not existing files:
<sorted list>
Skipped files due to modification since last analysis:
<sorted list>
```

Each block only if non-empty. ("Updated files" is printed in listing mode
too, meaning "files that have fixes".)

Exit code: 0, except the errors above.
