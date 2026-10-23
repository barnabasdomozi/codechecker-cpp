# Reference: captured output, reproducers, failures, timeouts

Usage: [../usage/analysis-debugging.md](../usage/analysis-debugging.md)

Python source: `analyzer/codechecker_analyzer/analysis_manager.py`,
`codechecker_common/process.py`, `tools/tu_collector/tu_collector/tu_collector.py`.

## 1. `--capture-analysis-output`

For **successful** jobs only: write
`<out>/success/<result base>.stdout.txt` and `.stderr.txt` with the
analyzer's stdout/stderr; each file only if its content is non-empty.
`<result base>` = result file name (`<src>_<analyzer>_<hash>.plist`).
`success/` is created at worker start and deleted at the end if empty.

## 2. Debug zips

Zip file name: `<result base>[_CTU]<failure>.zip`:
- `_CTU` if the job ran with CTU;
- `<failure>`: `""` if return code 0, `_compile_error` if 1, `_crash` if
  254, `_unknown` otherwise (including `-1` for timeouts).
- After a failed reanalysis without CTU: `<result base>.zip`.

Location: `failed/` for failed jobs; `reproducer/` for every job
(successful and failed) when `--generate-reproducer` is given (then
`failed/` gets nothing). Existing zips of the same name are deleted before
the job.

Content (in this order):
1. The translation unit sources, via the tu_collector zip routine
   ([tu-collector.md](tu-collector.md)), for a compilation database of:
   - the job's action `{file: source, command: original command, directory}`;
   - for every file mentioned in the analyzer output (CTU loaded ASTs for
     clangsa, `path:line:col: ` prefixes for clang-tidy), the build action
     for `(abs path, same target)` if present.
   This writes `compilation_database.json` and
   `sources-root/<absolute path>` entries.
2. The mentioned files themselves (`sources-root/...`).
3. Plain entries:
   - `stdout`, `stderr`: analyzer output;
   - `build-action`: original compile command;
   - `analyzer-command`: shell-quoted analyzer command joined with spaces;
   - `return-code`: the return code as text;
   - `gcc-toolchain-path`: the `--gcc-toolchain=` value of the original
     command, if present;
   - `compiler_info.json`: copy of `<out>/compiler_info.json` if it exists.

## 3. Failure handling

See [analyze.md](analyze.md) §4.1 step 4.8. Summary:
- ERROR `Analyzing <file> with <analyzer> [CTU] failed!` plus stdout/stderr
  unless `-q`;
- the result handler still runs when `clang-diagnostic-error` is enabled, so
  compiler errors become `clang-diagnostic-error` reports;
- the stale `<out>/<result base>.plist` from an earlier run is deleted;
- the job counts as failed (`analyzer_statistics.failed`), `analyze` exits 3.

Return code meanings (from the analyzer process): 0 success, 1 "compile
error", 254 "crash", other → unknown.

## 4. `--timeout N`

When the analyzer process starts, a timer of N seconds starts (only if
N > 0). On expiry: log debug `Process <pid> has ran for too long, killing it!`
and stop the **process tree** of the analyzer:
1. collect all descendants (recursively);
2. send SIGTERM to the analyzer process;
3. wait up to 5 s for the descendants to exit; SIGKILL those still alive;
4. poll once per second, up to ~11 s, until the analyzer process is gone;
   then warn `Waiting for process <pid> to stop has been timed out(timeout = 10)! Process is still running!`
   and give up.

After the process exits, the job sees the kill:
- WARNING `Analyzer ran too long, exceeding time limit of N seconds.`
- WARNING `Considering this analysis as failed...`
- return code forced to `-1`, stderr prefixed with
  `>>> CodeChecker: Analysis timed out after N seconds. <<<\n`.
If the process ends in time, the timer is cancelled.

## 5. Verbose logging

`--verbose debug` → precise log format
([common-infrastructure.md](common-infrastructure.md) §6), including the
analyzer command and environment at DEBUG level. `--verbose debug_analyzer`
additionally (level 15) logs each analyzer command (shell-quoted), the
clang help commands, CTU/statistics commands and the analyzer output of
successful jobs.

## 6. `.plist.err` files

Written next to the result file for every non-zero return code; see
[analyzer-drivers.md](analyzer-drivers.md) §6.
