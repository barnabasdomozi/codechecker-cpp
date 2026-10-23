# Cross translation unit (CTU) analysis

Normally, the Clang Static Analyzer looks at one source file (translation
unit) at a time. It cannot see into functions defined in other files. CTU
analysis lets it follow calls into other translation units.

In the [demo project](demo-project.md), `divide()` in `main.c` calls
`get_zero()` from `lib/util.c`. Only CTU analysis finds this division by zero.

CTU runs in two phases:
1. **collect**: for every source file, dump an AST (or the command to build
   one) and a map of the functions it defines (`externalDefMap.txt`).
2. **analyze**: run Clang SA with access to the collected data.

CTU works with the `clangsa` analyzer only. It requires one compilation
command per source file, so `--compile-uniqueing` defaults to `alpha` in CTU
mode.

```
--ctu | --ctu-all          both phases; the collected data is deleted afterwards
--ctu-collect              phase 1 only
--ctu-analyze              phase 2 only, using existing data
--ctu-reanalyze-on-failure
--ctu-ast-mode {load-from-pch,parse-on-demand}
```

## Run both phases ✅

```sh
CodeChecker analyze compile_commands.json -o reports --ctu --analyzers clangsa
CodeChecker parse reports
```

```
[HIGH] ~/demo/main.c:6:12: Division by zero [core.DivideZero]
[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]
```

Without `--ctu`, only `main.c:11` is found.

## Run the phases separately ✅

```sh
CodeChecker analyze compile_commands.json -o reports --ctu-collect --analyzers clangsa
ls reports/ctu-dir/*          # e.g. reports/ctu-dir/x86_64/{ast, externalDefMap.txt}
CodeChecker analyze compile_commands.json -o reports --ctu-analyze --analyzers clangsa
```

`--ctu-collect` prints `Pre-analysis finished.` and does not run the
analyzers. `--ctu-analyze` reuses `reports/ctu-dir` and does not delete it.
This lets you collect once and analyze many times, or split the phases across
CI jobs.

After CTU analysis, `reports/ctu_connections/` records which other
translation units each analysis used. `tu_collector --ctu-deps-dir` reads it
(see [tu-collector.md](tu-collector.md)).

## AST mode ✅

```sh
CodeChecker analyze compile_commands.json -o reports --ctu --ctu-ast-mode load-from-pch --analyzers clangsa
```

| Mode | Behavior |
|---|---|
| `parse-on-demand` (default) | The collect phase saves only the commands needed to parse each file. ASTs are parsed during analysis. Uses less disk space, more CPU. |
| `load-from-pch` | The collect phase writes serialized ASTs (PCH) to disk. Uses a lot of disk space. |

## Retry without CTU on failure ✅

```sh
CodeChecker analyze compile_commands.json -o reports --ctu --ctu-reanalyze-on-failure --analyzers clangsa
```

If the CTU analysis of a file fails, analyze it again without CTU.

## With `check` ✅

```sh
CodeChecker check -l compile_commands.json --ctu --analyzers clangsa
```

## Merging definition maps by hand

CodeChecker merges the per-file definition maps itself. The
`merge-clang-extdef-mappings` tool does the same as a separate step (see
[merge-clang-extdef-mappings.md](merge-clang-extdef-mappings.md)).
