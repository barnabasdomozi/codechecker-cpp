# Reference: `memory_safety_reporter`

Usage: [../usage/memory-safety-reporter.md](../usage/memory-safety-reporter.md)

Python source: `tools/memory-safety-reporter/memory_safety_reporter.py`
(installed as `bin/memory_safety_reporter`).

## 1. Command line

```
memory_safety_reporter -r REPORT_DIRECTORY [-o OUTPUT] [-p PRODUCT] [-v REVISION]
                       [-b BINARY] [-d BUILD_ID] [-t TIMESTAMP] [-x {gztar,zip}]
```

Plain `argparse` (errors exit 2). Defaults: `-o memory_safety_report`,
`-x zip`. All values are free text; nothing is validated or interpreted.
Logging (`[<LEVEL>] - <msg>`) to stdout.

## 2. Algorithm

1. Base name = `"_".join([OUTPUT] + [the given values of -p, -v, -b, -d,
   -t in this order])` (absent options are left out; empty strings are
   kept).
2. `REPORT_DIRECTORY` not a directory → ERROR `Report directory '<dir>'
   does not exist!`, exit 1.
3. In a new temporary directory `T`, create `T/<base name>/` (= `B`; if the
   base name contains `/`, sub-directories are created) and:
   1. run `CodeChecker parse REPORT_DIRECTORY -e sarif -o B/reports.sarif`
      (the `CodeChecker` found in `PATH`; its output goes to this
      program's stdout/stderr). Exit codes 0 and 2 are accepted, others →
      `RuntimeError("Failed to parse reports directory")` (traceback,
      exit 1);
   2. copy `REPORT_DIRECTORY/metadata.json` to `B/` (missing →
      `FileNotFoundError`, exit 1);
   3. run `CodeChecker checkers --guideline memory-safety -o json --details`
      with stderr merged into stdout; non-zero exit →
      `RuntimeError("Failed to get checker details")`; write the captured
      bytes to `B/checker_details.json`;
   4. copy the directory `REPORT_DIRECTORY/conf` recursively to `B/config`
      (missing → `FileNotFoundError`, exit 1);
   5. write `B/CHECKSUMS.sha256`: walk `B` recursively; for every file
      except `CHECKSUMS.sha256` itself, a line
      `<sha256 hex>\t<path relative to B>\n` (walk order; the checksum
      file exists, empty, while walking).
4. Print `Creating package with name:  <base name>` (two spaces: `print`
   with two arguments).
5. Create the archive `<base name>.zip` (or `.tar.gz` for `gztar`) relative
   to the **current directory**, containing the directory `<base name>/`
   with its contents (archive root = `T`). An existing archive is
   overwritten. The temporary directory is removed.

Exit 0.

The archive's content is therefore: `reports.sarif`
([parse-export.md](parse-export.md) SARIF), `metadata.json`
([analyze.md](analyze.md)), `checker_details.json` (the `checkers -o json
--details` list, [checkers.md](checkers.md)), `config/` (the analyzer
configuration copies, e.g. `.clang-tidy` files and skip files) and
`CHECKSUMS.sha256`.
