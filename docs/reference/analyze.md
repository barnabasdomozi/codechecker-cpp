# Reference: `CodeChecker analyze`

Usage: [../usage/analyze.md](../usage/analyze.md)

Python source: `analyzer/codechecker_analyzer/cli/analyze.py`,
`analyzer.py`, `analysis_manager.py`, `pre_analysis_manager.py`,
`compilation_database.py`, `buildlog/log_parser.py`.

Related reference pages: [analyzer-drivers.md](analyzer-drivers.md) (per
analyzer commands), [checker-selection.md](checker-selection.md),
[analyzer-configuration.md](analyzer-configuration.md),
[compiler-info.md](compiler-info.md),
[skip-and-file-filters.md](skip-and-file-filters.md),
[ctu-analysis.md](ctu-analysis.md),
[statistical-analysis.md](statistical-analysis.md),
[makefile-mode.md](makefile-mode.md),
[analysis-debugging.md](analysis-debugging.md),
[report-format.md](report-format.md).

## 1. Options and defaults

| Option | Dest | Default |
|---|---|---|
| `input` (positional) | must exist; made absolute | – |
| `-o/--output` | output dir (required) | – |
| `-j/--jobs` | int | number of CPUs |
| `-i/--ignore/--skip FILE` | skipfile | absent |
| `--drop-reports-from-skipped-files` | flag | false |
| `--file F...` | file filters | absent |
| `--review-status-config FILE` | must exist | absent |
| `--compiler-info-file F`, `--dump-compiler-info-file` | | absent / false |
| `--keep-gcc-include-fixed`, `--keep-gcc-intrin`, `--add-gcc-include-dirs-with-isystem` | flags | false |
| `-t/--type/--output-format` | `plist` only | `plist` |
| `--makefile` | flag | false |
| `-q/--quiet`, `-c/--clean` | flags | absent |
| `--compile-uniqueing` | `none`, `strict`, `symlink`, `alpha` or regex | `none` |
| `--report-hash` | `context-free`, `context-free-v2`, `diagnostic-message` | absent |
| `-n/--name` | run name | absent |
| `--analyzers A...` | subset of the 5 | all available |
| `--capture-analysis-output`, `--generate-reproducer` | flags | absent |
| `--config FILE` | | absent |
| `--saargs`, `--tidyargs`, `--cppcheckargs FILE` | deprecated | absent |
| `--analyzer-config A:K=V ...`, `--checker-config A:C:O=V ...` | ordered, de-duplicated lists | `[]` / absent |
| `--timeout N` | seconds | absent |
| `--z3-refutation on/off` | | `on` if Clang supports `crosscheck-with-z3`, else `off` |
| `--ctu`/`--ctu-all`, `--ctu-collect`, `--ctu-analyze` | mutually exclusive; phases `[collect, analyze]` = `[T,T]`, `[T,F]`, `[F,T]` | absent |
| `--ctu-reanalyze-on-failure` | flag | absent |
| `--ctu-ast-mode` | `load-from-pch`, `parse-on-demand` | absent |
| `--stats-collect DIR`, `--stats-use DIR`, `--stats` | | absent |
| `--stats-min-sample-count N`, `--stats-relevance-threshold R` | | `10` / `0.85` if Clang has statistics checkers, else absent |
| `-e/--enable X`, `-d/--disable X` | ordered list of `(X, enabled)` | absent |
| `--enable-all`, `--disable-all`, `--no-missing-checker-error` | flags | absent |

`--analyzer-config`/`--checker-config` values are validated by regex at parse
time (`(.+):(.+)=(.+)` and `(.+):(.+):(.+)=(.+)`, greedy, so the value is
after the **last** `=`); a mismatch is an argparse error (exit 1). Repeating
an identical item moves it to the end of the list.

Note: computing the `--z3-refutation` and statistics defaults runs Clang
while building the argument parser, i.e. on every `analyze`/`check` start.

## 2. Main flow

1. Set up logging.
2. Convert deprecated flags: `--saargs F` → append
   `clangsa:cc-verbatim-args-file=F` to `--analyzer-config` (same for
   `--tidyargs` → `clang-tidy`, `--cppcheckargs` → `cppcheck`), each with a
   deprecation warning.
3. Validate `--analyzer-config` and `--checker-config` (see
   [analyzer-configuration.md](analyzer-configuration.md) §3). On failure,
   unless `--no-missing-checker-error`: log
   `Although it is not recommended, if you want to suppress errors relating to unknown analyzer/checker configs, consider using the option '--no-missing-checker-error'`
   and exit 1.
4. `--ctu-ast-mode` without a CTU phase option →
   `Analyzer option 'ctu-ast-mode' requires CTU mode enabled`, exit 1.
5. Capability checks (exit 1 with the hint
   `hint: Maybe CodeChecker found the wrong analyzer binary?`):
   - any `stats*` option present but Clang has no `statisticscollector`
     checker → statistics error (see
     [statistical-analysis.md](statistical-analysis.md)). (The two numeric
     defaults exist only when Clang is capable, so they never trigger this.)
     The option attributes are scanned in alphabetical order and only the
     first `ctu*`/`stats*` problem is reported;
   - any `ctu*` option present but CTU is not supported
     (`CrossTranslation Unit options can only be enabled if clang itself supports it!`);
   - `--ctu-ast-mode` given but `-analyzer-config-help` lacks
     `ctu-invocation-list`;
   - `--z3-refutation on` but Clang does not accept
     `-analyzer-constraints=unsupported-z3` or `-analyzer-constraints=z3`
     (`Z3 refutation cannot be enabled as Clang was not compiled with Z3!`).
6. Config file existence check (see [config-file.md](config-file.md)).
7. Output path: made absolute. If it exists and is not a directory (and
   not dump mode) → `The given output path is not a directory: <p>`, exit 1.
8. `--enable-all` → INFO `'--enable-all' was supplied for this analysis.`
   `--disable-all` → insert `("default", disabled)` at the **front** of the
   ordered checker list.
9. If CTU is enabled and `--compile-uniqueing` contains `none` → use
   `alpha`.
10. `--compiler-info-file` must exist (`Compiler info file <f> does not exist`,
    exit 1).
11. **Gather the compilation database** from `input` (§3). `None` →
    `Found no compilation commands in '<input>'`, exit 1.
12. Build skip handlers (see
    [skip-and-file-filters.md](skip-and-file-filters.md)); the "filter
    handlers" are the same handlers if `--drop-reports-from-skipped-files`,
    else none.
13. Review status handler for the output dir; load
    `--review-status-config` (a format error → log it, exit 1).
14. Pre-analysis skip handling: if only `--ctu-collect` or `--stats-collect`
    is given, the skip handlers also apply to the pre-analysis.
15. If the collect phase of CTU is requested and `<out>/ctu-dir` exists,
    delete it.
16. `-c`: if the output dir exists, delete it entirely (INFO
    `Previous analysis results in '<out>' have been removed, overwriting with current result`).
17. Create the output dir (not in dump mode).
18. **Parse the compilation database** into build actions (uniqueing,
    skipping, compiler info; see [compiler-info.md](compiler-info.md)).
19. No actions → warnings `No analysis is required.` and
    `There were no compilation commands in the provided compilation database or all of them were skipped.`,
    **exit 0**.
20. `--dump-compiler-info-file` → write the compiler info JSON to `-o`
    (a file path in this mode), exit 0. Otherwise write
    `<out>/compiler_info.json`.
21. Create `<out>/fixit/`. Write `<out>/unique_compile_commands.json`: a JSON
    array of `{"command", "directory", "file"}` for each kept action
    (`file` is the absolute source path).
22. Build the metadata object (§5); read the previous `<out>/metadata.json`
    (if any) and copy its `result_source_files` into the new metadata.
23. Compute counts: total entries, skipped, removed by uniqueing, to analyze.
24. **Perform the analysis** (§4).
25. Write side files:
    - `<out>/skip_file`: deleted, then a copy of the skipfile if given.
    - `<out>/review_status.yaml`: deleted, then a copy of the
      review-status config if given.
    - `<out>/conf/`: deleted and re-created; copies (by base name) of the
      skipfile, review-status config, `--config` file, and every
      `cc-verbatim-args-file` value.
26. Metadata cleanup: for every `result_source_files` entry of the previous
    metadata whose **source file** no longer exists, delete the plist file and
    its entry (INFO `Remove plist file '<p>' because it refers to a source file ('<s>') which was removed.`).
27. Write `<out>/metadata.json` and `<out>/compile_cmd.json` (the gathered
    compilation database, indented by 2).
28. Exit code: **3** if any analyzer's `failed` count is non-zero, else 0.

## 3. Gathering the compilation database

Input kinds, tried in order:

1. **JSON file**: if `input` loads as JSON, use it (any JSON; a
   non-list value later fails as "not valid").
2. **C-family source file** (exists and has one of the C/C++/ObjC
   extensions): find the closest `compile_commands.json` in the **parent
   directories** of the file (the file's own directory first, then upwards to
   `/`). Use only its entries whose `normpath(directory/file)` equals the
   absolute source path. No database → empty list (→ step 19, exit 0).
3. **Directory**: collect every `compile_commands.json` inside the directory
   tree and in all parent directories. For each entry of each database whose
   `directory/file` lies inside `input`, keep it if that database is the
   "closest" one for the file: the database whose directory is the longest
   common path of all database directories plus the file. (In practice: the
   database in the deepest directory that contains the file.)
4. Otherwise `None` (→ exit 1).

Entries with `arguments` (list) instead of `command` get a `command` built by
shell-quoting and space-joining the arguments.

## 4. Performing the analysis

1. Availability check on `--analyzers` (or all). With an explicit list,
   any unavailable analyzer prints a warning per analyzer and
   `Failed to run command because the given analyzer(s) cannot be found on your machine!`,
   exit 1. Without a list, exit 1 only if **no** analyzer works
   (`... because no analyzers can be found on your machine!`).
2. Sort analyzers in registry order (`clangsa`, `clang-tidy`, `cppcheck`,
   `gcc`, `infer`).
3. CTU requested but `clangsa` not available → error
   `CTU can only be used with the clang static analyzer.` and return (the
   command still exits 0 at the end, since no analyzer failed).
4. Build one configuration (checker state table + options) per analyzer
   ([checker-selection.md](checker-selection.md),
   [analyzer-configuration.md](analyzer-configuration.md)).
5. Drop analyzers with no enabled checker: INFO `No checkers enabled for <a>`.
   (Also stop warning about unavailable analyzers that would have had no
   enabled checkers.)
6. Create jobs: for each remaining analyzer **in order**, for each build
   action → (action, analyzer).
7. **Unknown checker names** (only for `-e`/`-d` values):
   - "available names" = profile names from `descriptions.json` + every
     checker name of every remaining analyzer;
   - a value `prefix:P` is missing if no available name equals `P` or
     starts with `P.` or `P-`;
   - a value `checker:C` is treated as plain `C`; any other value with a `:`
     (labels, `profile:`, `guideline:`) is not checked;
   - a plain value `X` is missing if no available name matches the regex
     `^<escaped X>\b`.
   - Missing items: `No prefix(es) with these names was found: <list>.` /
     `No checker(s) with these names was found: <list>.` as errors, followed
     by the `--no-missing-checker-error` hint and exit 1; with
     `--no-missing-checker-error` they are warnings and the analysis
     continues.
8. Metadata per analyzer: `checkers: {name: enabled_bool}` for every checker
   in the state table, `analyzer_statistics: {failed: 0, failed_sources: [],
   successful: 0, successful_sources: [], version: "<binary version>"}`.
   INFO `Enabled checker list can be found in <out>/metadata.json`.
9. `--makefile` → write the Makefile ([makefile-mode.md](makefile-mode.md))
   and return.
10. CTU: with the collect phase, delete `ctu-dir`; with only the analyze
    phase and no `ctu-dir` → `CTU directory: '<d>' does not exist.` and return.
11. Pre-analysis (CTU collect and/or statistics) on the `clangsa` jobs; see
    [ctu-analysis.md](ctu-analysis.md) and
    [statistical-analysis.md](statistical-analysis.md).
12. `--stats-collect` → return after pre-analysis.
13. `--stats-use D` → statistics data = `{stats_out_dir: D}`.
14. If CTU analyze phase, or statistics, or no CTU at all:
    INFO `Starting static analysis ...`, run the workers (§4.1), then the
    INFO lines `Analysis finished.`,
    `To view results in the terminal use the "CodeChecker parse" command.`,
    `To store results use the "CodeChecker store" command.`,
    `See --help and the user guide for further options about parsing and storing the reports.`,
    `----=================----`.
15. INFO `Analysis length: <seconds rounded to 2 decimals> sec.`
16. With an explicit `--analyzers` list, warn about unavailable analyzers
    again.
17. `timestamps: {begin, end}` (Unix epoch floats) in the metadata.
18. With both CTU phases (`--ctu`), delete `ctu-dir`.

### 4.1 Workers

1. Remove jobs whose source is skipped by the skip handlers.
2. Create `<out>/failed`, `<out>/success`, `<out>/cppcheck`,
   `<out>/ctu_connections`, and `<out>/reproducer` (only with
   `--generate-reproducer`).
3. Run jobs in a pool of `-j` processes, one job per task, in job order.
   Progress counter starts at 1.
4. Per job ("check"):
   1. Construct the analyzer object; with statistics data and `clangsa`, add
      the statistics checker config if `<stats_dir>/SpecialReturn.yaml`
      exists (see [statistical-analysis.md](statistical-analysis.md)).
   2. Build the command ([analyzer-drivers.md](analyzer-drivers.md)).
   3. Run it; with `--timeout N > 0`, a timer kills the whole process tree
      after N seconds. A killed run gets return code `-1`, warnings
      `Analyzer ran too long, exceeding time limit of N seconds.` and
      `Considering this analysis as failed...`, and stderr prefixed with
      `>>> CodeChecker: Analysis timed out after N seconds. <<<\n`.
   4. Analyzer-specific post-processing (`post_analyze`).
   5. `result_file` = result file path with `\ ` replaced by space.
   6. Zip name for debug archives:
      `<basename(result_file)>[_CTU]<failure>.zip`, with `_CTU` if CTU was
      active and `<failure>` = `""` (rc 0), `_compile_error` (rc 1), `_crash`
      (rc 254), else `_unknown`. Delete any existing zip of that name in
      `failed/` and `reproducer/`.
   7. **Success (rc 0)**: optionally save stdout/stderr to
      `success/<result base>.stdout.txt` / `.stderr.txt` (only non-empty
      ones; `--capture-analysis-output`); run the result handler
      ([analyzer-drivers.md](analyzer-drivers.md) §6); write
      `<result_file>.source` containing the analyzed source path (`\ `
      replaced by space) and a newline; if the handler wrote to a different
      path than `result_file`, rename it. With `--generate-reproducer`, also
      write a reproducer zip to `reproducer/`.
      INFO `[<n>/<total>] <analyzer> analyzed <basename> successfully.`
   8. **Failure**: ERROR `Analyzing <basename> with <analyzer> [CTU] failed!`;
      unless `-q`, ERROR-log stdout and stderr. If the analyzer's
      `clang-diagnostic-error` checker is enabled (or the analyzer has no
      such checker), still run the result handler (so compiler errors become
      reports). Delete `<out>/<result base>.plist` from earlier runs. Write
      the debug zip to `failed/` (or `reproducer/` with
      `--generate-reproducer`); see
      [analysis-debugging.md](analysis-debugging.md).
      If CTU was active and `--ctu-reanalyze-on-failure`: ERROR
      `Try to reanalyze without CTU`, rebuild with CTU disabled, run again;
      success → as above with INFO
      `[n/total] <analyzer> analyzed <basename> without CTU successfully.`;
      failure → ERROR `Analyzing '<basename>' with <analyzer> without CTU failed.`
      and a zip named `<result base>.zip`.
   9. CTU connections: for clangsa, collect files from lines
      `CTU loaded AST file: <path>` in stdout/stderr (mapped back to source
      paths, see [ctu-analysis.md](ctu-analysis.md)); write them, one per
      line, to `ctu_connections/<result name>` (or delete that file if none).
   10. Unless `-q`: failed runs log stdout/stderr again as ERROR; successful
       runs log them at DEBUG_ANALYZER.
   11. Return `(return_code, skipped=False, reanalyzed, analyzer, result_file,
       source)`. `reanalyzed` = the result file existed before this run. An
       exception in a job → stack trace, return code 1.
5. After all jobs (the summary callback):
   - `successful`/`failed` counts and source lists per analyzer;
   - INFO block:
     ```
     ----==== Summary ====----
     Successfully analyzed
       <analyzer>: <count>
     Failed to analyze
       <analyzer>: <count>
     [Reanalyzed compilation commands: N]
     [Skipped compilation commands: N]
     ```
     (a section header is printed only if some analyzer has a non-zero count);
   - every `<out>/*.source` file is read into
     `result_source_files[<path without .source>] = <content stripped>` and
     deleted;
   - for every `failed/*.error` file, remove `<out>/<name>.plist` from
     `result_source_files`.
6. INFO `Total analyzed compilation commands: <to analyze>`; INFO
   `Skipped compilation commands: <n>` if anything was skipped (during parsing
   or by the workers); INFO `----=================----`.
7. Delete `success/` and `failed/` if empty.

## 5. `metadata.json`

```json
{
  "version": 2,
  "tools": [{
    "name": "codechecker",
    "action_num": <number of build actions>,
    "command": ["<argv0>", "analyze", ...],
    "version": "<git tag> (<git hash>)",
    "working_directory": "<cwd>",
    "output_path": "<absolute out dir>",
    "result_source_files": {"<abs plist path>": "<abs source path>", ...},
    "analyzers": {
      "<analyzer>": {
        "checkers": {"<checker>": true|false, ...},
        "analyzer_statistics": {
          "failed": N, "failed_sources": [...],
          "successful": N, "successful_sources": [...],
          "version": "<binary version>"
        }
      }
    },
    "run_name": "<-n value>",           // only with -n
    "skipped": N,
    "timestamps": {"begin": <float>, "end": <float>}
  }]
}
```

`command` is the process argument vector of the implementation (in the
Python version: `[".../cli.py", "analyze", ...]`).

Older metadata layouts (version 1, top-level `result_source_files`) are still
read for `result_source_files`.

## 6. Exit codes

| Code | When |
|---|---|
| 0 | Analysis finished (also: nothing to analyze, dump mode, CTU-without-clangsa) |
| 1 | Argument/config/capability errors, uniqueing errors, unknown checkers, invalid database |
| 3 | At least one job failed |
| 128+N | Interrupted by signal N |
