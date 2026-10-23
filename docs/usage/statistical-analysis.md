# Statistical analysis

Statistical analysis finds functions whose return value is almost always
checked, then reports the places where it is not checked. It has two phases:

1. **collect**: run the statistics collector checkers
   (`statisticsCollector.ReturnValueCheck`,
   `statisticsCollector.SpecialReturnValue`) over the whole project, then
   summarize the output into YAML files.
2. **use**: run the analysis with the statistics-based checkers, reading the
   YAML files.

```
--stats                         both phases; the statistical checkers are enabled automatically
--stats-collect STATS_OUTPUT    phase 1 only, results in STATS_OUTPUT; no analysis is run
--stats-use STATS_DIR           phase 2 only, using STATS_DIR
--stats-min-sample-count N      minimum number of calls to a function before its statistics count
--stats-relevance-threshold R   minimum ratio of calls that must have the property, e.g. 0.85
```

Examples:

```sh
CodeChecker analyze compile_commands.json -o reports --stats
CodeChecker analyze compile_commands.json -o reports --stats-collect ./stats
CodeChecker analyze compile_commands.json -o reports --stats-use ./stats
CodeChecker analyze compile_commands.json -o reports --stats --stats-min-sample-count 5 --stats-relevance-threshold 0.9
```

## Status on the test machine ❌

These options need a Clang build with the `statisticsCollector` checkers.
Upstream Clang 19 does not have them; Ericsson's Clang fork does. With
upstream Clang, all four commands above fail with exit code `1`:

```
[ERROR] - Statistics options can only be enabled if clang has statistics checkers available!
[INFO] - CodeChecker has not found Clang Static Analyzer checkers statisticsCollector.ReturnValueCheck and statisticsCollector.SpecialReturnValue
[INFO] - hint: Maybe CodeChecker found the wrong analyzer binary?
```

The port must give the same error when the checkers are missing.

## `post-process-stats` tool ✅ (partly)

The second step of phase 1 is also shipped as a separate tool. It reads the
raw collector output and writes the YAML files.

```
post-process-stats -i INPUT_FOLDER [--stats-min-sample-count N]
                   [--stats-relevance-threshold R] [-v] OUTPUT_DIR
```

```sh
post-process-stats -i ./pre_processed_stats ./stats
```

With an empty input folder (the only case that can be tested without the
collector checkers):

```
[INFO] - Starting to post-process statistical results...
[WARNING] - No output files were found to collect statistics.
[INFO] - Done.
```

Exit code `0`.
