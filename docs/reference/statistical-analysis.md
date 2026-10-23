# Reference: statistical analysis and `post-process-stats`

Usage: [../usage/statistical-analysis.md](../usage/statistical-analysis.md)

Python source: `analyzer/codechecker_analyzer/analyzers/clangsa/statistics.py`,
`pre_analysis_manager.py`, `analyzer.py` (`__get_statistics_data`),
`analysis_manager.py` (`prepare_check`),
`analyzer/tools/statistics_collector/`.

## 1. Capability

Statistics are available if the clangsa checker list (with alpha **and**
developer checkers) contains a name matching `.+statisticscollector.+`
(case-sensitive, so `alpha.ericsson.statisticscollector...`-style names).
Otherwise any statistics option → error
`Statistics options can only be enabled if clang has statistics checkers available!`,
INFO `CodeChecker has not found Clang Static Analyzer checkers statisticsCollector.ReturnValueCheck and statisticsCollector.SpecialReturnValue`,
INFO `hint: Maybe CodeChecker found the wrong analyzer binary?`, exit 1.

Defaults when capable: `--stats-min-sample-count 10`,
`--stats-relevance-threshold 0.85`.

## 2. Configuration

| Option | statistics data |
|---|---|
| `--stats` | `stats_out_dir = <out>/stats` |
| `--stats-collect D` | `stats_out_dir = D` |
| both | `D` wins |
| `--stats-use D` | (after pre-analysis) `stats_out_dir = D`, no collection |

`stat_tmp_dir = <stats_out_dir>/tmp`. Validation: min sample count must be
`> 1` (else ERROR `stats_min_sample_count must be greater than 1.` and
statistics are disabled); relevance threshold must be strictly between 0 and
1 (else ERROR and disabled).

Checker states: with `--stats`, the `statisticsbased.SpecialReturnValue` and
`statisticsbased.UncheckedReturnValue` checkers are enabled (unless
disabled by the user); the collector checkers are always disabled for the
normal analysis (see [checker-selection.md](checker-selection.md) §6).

## 3. Collect (pre-analysis)

For each clangsa action (pool of `-j`; `stat_tmp_dir` is deleted and
re-created first):

```
<clang> -c -x <lang> --analyze -Qunused-arguments --analyzer-output text
  [plugin flags]
  <verbatim args> <analyzer_options>
  -Xclang -analyzer-checker=<collector>      (each available checker containing
                                              statisticscollector.SpecialReturnValue
                                              or statisticscollector.ReturnValueCheck)
  [--target=<t>] [<compiler_standard> if no -std]
  <implicit include flag> <dir>...
  <source>
```

(No collector checker found → this action is not collected.) Non-zero exit →
ERROR `Failed to collect statistics for <source>`. Otherwise stdout followed
by stderr is written to `<stat_tmp_dir>/<basename><uuid4>.stat`.

Then `post_process_stats.process(stat_tmp_dir, stats_out_dir, min, threshold)`
(§4) and `stat_tmp_dir` is deleted.

`--stats-collect` stops after this step.

## 4. Post-processing (`post-process-stats`)

```
post-process-stats -i INPUT_DIR [--stats-min-sample-count N=10]
                   [--stats-relevance-threshold R=0.85] [-v] OUTPUT_DIR
```

1. Create `OUTPUT_DIR` if it does not exist (single level).
2. No `INPUT_DIR` → debug log, return. Collect the regular files in it. None →
   WARNING `No output files were found to collect statistics.`, return.
3. Read all files; take the **set** of distinct lines (duplicates across
   files count once).
4. For each line:
   - Return value: regex
     `.*warning: Return Value Check:.*:[0-9]*:[0-9]*.*,(.*),([0,1])`;
     `total[f] += 1`, `unchecked[f] += int(group 2)`.
   - Special return value: regex
     `.*warning: Special Return Value:.*:[0-9]*:[0-9]*.*,(.*),([0,1]),([0,1])`;
     `total[f] += 1`, `negative[f] += g2`, `null[f] += g3`.
5. Write `OUTPUT_DIR/UncheckedReturn.yaml`:

   ```
   #
   # UncheckedReturn metadata format 1.0
   - <function>
   ```

   one line per function `f` (sorted by name) with
   `threshold < 1 - unchecked/total < 1` and `total >= min`.
6. Write `OUTPUT_DIR/SpecialReturn.yaml`:

   ```
   #
   # SpecialReturn metadata format 1.0
   {name: <f>, relation: LT, value: 0}
   {name: <f>, relation: EQ, value: 0}
   ```

   first all functions (sorted) with `threshold < negative/total < 1` and
   `total >= min` (relation `LT`), then all with
   `threshold < null/total < 1` and `total >= min` (relation `EQ`).

The command logs `[INFO] - Starting to post-process statistical results...`
and `[INFO] - Done.` (format `[LEVEL] - message`, stderr). Exit code 0.

## 5. Use (analysis)

For each clangsa job with statistics data: if
`<stats_out_dir>/SpecialReturn.yaml` exists, add before the checker list:

```
-Xclang -analyzer-config -Xclang alpha.ericsson.statisticsbased:APIMetadataPath=<stats_out_dir>
```

(one entry for both statistics checkers). Otherwise debug
`No checker statistics file was found for statisticsbased.SpecialReturnValue`.
