# Analyze a project in one step (`CodeChecker check`)

`CodeChecker check` runs [`log`](log.md), [`analyze`](analyze.md) and
[`parse`](parse.md) in a row: it records the build, analyzes it and prints the
results. It is the quickest way to analyze a project.

```
CodeChecker check (-b BUILD_COMMAND | -l COMPILATION_DATABASE) [-o OUTPUT_DIR]
                  [analyze options] [parse options]
```

`check` accepts almost every `analyze` option (checkers, analyzer config, CTU,
statistics, skipfile, `--file`, compile uniqueing, report hash, timeout,
`--config`, ...) and these `parse` options: `--print-steps`, `--suppress`,
`--trim-path-prefix`, `--review-status`. It has no `--export`; run
`CodeChecker parse` on the output folder for HTML/JSON/etc.

## Build and analyze ✅

```sh
cd ~/demo
make clean
CodeChecker check -b "make"
```

The output is the build output, the analysis progress, and then the same
report listing as `CodeChecker parse` (see [parse.md](parse.md)). The exit
code is `2` because reports were found.

`--use-absolute-ldpreload-path` and `-q` (hide build output) apply to the log
step.

## Use an existing compilation database ✅

```sh
CodeChecker check -l compile_commands.json
```

## Keep the results ✅

```sh
CodeChecker check -l compile_commands.json -o ./reports
```

Without `-o`, results go to a temporary folder (e.g. `/tmp/tmpbs6ut45p`) that
is deleted at the end ✅.
With `-o`, you can later `parse`, `store` or `fixit` the folder.

## Combine analyze and parse options ✅

```sh
CodeChecker check -l compile_commands.json -o ./reports --print-steps
CodeChecker check -l compile_commands.json --ctu --analyzers clangsa --trim-path-prefix "$HOME/demo/"
```

```
[HIGH] main.c:6:12: Division by zero [core.DivideZero]
[HIGH] main.c:11:12: Division by zero [core.DivideZero]
```

```sh
CodeChecker check -l compile_commands.json -e modernize-use-nullptr \
  --review-status unreviewed confirmed false_positive intentional suppress
```

```
[LOW] ~/demo/style.cpp:4:12: use nullptr [modernize-use-nullptr]
[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable]
[HIGH] ~/demo/main.c:25:10: variable 'x' is uninitialized when used here [clang-diagnostic-uninitialized]
[HIGH] ~/demo/style.cpp:11:12: Division by zero [core.DivideZero] [False positive]
[HIGH] ~/demo/style.cpp:17:12: Division by zero [core.DivideZero] [False positive]
[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]
```

```sh
CodeChecker check -l compile_commands.json --suppress suppress.txt
```

## Exit codes ✅

| Code | Meaning |
|---|---|
| 0 | No reports and every analysis succeeded |
| 1 | CodeChecker error |
| 2 | At least one report printed (also when some analysis failed ✅) |
| 3 | No reports, but at least one analysis failed |
| other | With `-b`: the build command's non-zero exit code |
