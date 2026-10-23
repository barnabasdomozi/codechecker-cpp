# Configure analyzers and checkers

`analyze` and `check` pass options to the analyzer tools in four ways:

| Option | Applies to |
|---|---|
| `--analyzer-config ANALYZER:KEY=VALUE ...` | The whole analyzer |
| `--checker-config ANALYZER:CHECKER:OPTION=VALUE ...` | One checker |
| `--analyzer-config ANALYZER:cc-verbatim-args-file=FILE` | Raw flags from a file, passed unchanged |
| `--saargs`, `--tidyargs`, `--cppcheckargs FILE` | Deprecated forms of the verbatim file |

List the available keys with `CodeChecker analyzers --analyzer-config <analyzer>`
and `CodeChecker checkers --checker-config` (see [analyzers.md](analyzers.md)
and [checkers.md](checkers.md)).

## Analyzer options ✅

```sh
CodeChecker analyze compile_commands.json -o reports \
  --analyzer-config clangsa:mode=shallow clang-tidy:HeaderFilterRegex=.*
```

Several `ANALYZER:KEY=VALUE` items can follow one `--analyzer-config`.

Commonly used keys:
- `clangsa:<any -analyzer-config option>`, e.g. `mode=shallow`,
  `max-nodes=...`, `unroll-loops=true`
- `clang-tidy:Checks`, `HeaderFilterRegex`, `WarningsAsErrors`, ...
- `clang-tidy:take-config-from-directory=true` (see below)
- `cppcheck:addons`, `libraries`, `platform`, `inconclusive` ❌ (not tested)
- `<any analyzer>:cc-verbatim-args-file=<file>`

## Checker options ✅

The format is `ANALYZER:CHECKER:OPTION=VALUE`, with **colons** as separators:

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy \
  -e readability-identifier-length \
  --checker-config clang-tidy:readability-identifier-length:MinimumVariableNameLength=1
```

With the default minimum length, the demo has 12 `readability-identifier-length`
reports. With `MinimumVariableNameLength=1` it has 5.

The Clang-Tidy style `CHECKER.OPTION` is rejected:

```
CodeChecker analyze: error: argument --checker-config: Checker option in wrong format:
clang-tidy:readability-identifier-length.MinimumVariableNameLength=1, should be <analyzer>:<checker>:<option>=<value>
```

## Pass raw flags from a file ✅

```sh
echo '-checks=-*,modernize-use-nullptr' > tidy_args.txt
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy \
  --analyzer-config clang-tidy:cc-verbatim-args-file=tidy_args.txt
```

Only `modernize-use-nullptr` reports remain. For Clang SA, the file holds
compiler-style flags, e.g. `-DFOO=1`:

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clangsa \
  --analyzer-config clangsa:cc-verbatim-args-file=sa.txt
```

### Deprecated forms ✅

```sh
CodeChecker analyze compile_commands.json -o reports --saargs sa.txt         # = clangsa:cc-verbatim-args-file
CodeChecker analyze compile_commands.json -o reports --tidyargs tidy_args.txt  # = clang-tidy:cc-verbatim-args-file
CodeChecker analyze compile_commands.json -o reports --cppcheckargs cc.txt   # = cppcheck:cc-verbatim-args-file  (❌ not tested)
```

These still work in 6.30.0 and give the same results.

## Use a `.clang-tidy` file ✅

```sh
printf 'Checks: "-*,modernize-use-nullptr"\n' > .clang-tidy
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy \
  --analyzer-config clang-tidy:take-config-from-directory=true
```

```
1 [modernize-use-nullptr]
```

With this option, CodeChecker does not pass `-checks` to clang-tidy, so
clang-tidy reads the `.clang-tidy` file nearest each source file. `-e`/`-d`
then have no effect on clang-tidy. You can generate a starting file with
`CodeChecker analyzers --dump-config clang-tidy > .clang-tidy`.

## Z3 refutation ✅

```sh
CodeChecker analyze compile_commands.json -o reports --z3-refutation off
```

`on` (default) re-checks Clang SA findings with the Z3 solver to remove false
positives. It only has an effect if Clang is built with Z3.

## Taint analysis configuration ❌

A Clang SA taint configuration file is set with
`--checker-config clangsa:alpha.security.taint.TaintPropagation:Config=<file.yaml>`.
Not tested.
