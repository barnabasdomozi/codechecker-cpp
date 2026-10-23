# Choose which checkers run

`analyze` and `check` take the same options for turning checkers on and off.
Without any of them, the checkers labeled `profile:default` run.

```
-e, --enable  checker | prefix | profile | guideline | label:value
-d, --disable checker | prefix | profile | guideline | label:value
--enable-all
--disable-all
--no-missing-checker-error
```

The options are applied **in the order given**, from broad to narrow. For
example, `-e core -d core.uninitialized -e core.uninitialized.Assign` turns on
every `core` checker, then turns off the `core.uninitialized` group, then
turns `core.uninitialized.Assign` back on.

Results below are from the [demo project](demo-project.md), as counts per
checker from `CodeChecker parse`.

## Enable one checker ✅

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy -e modernize-use-nullptr
```

```
1 [clang-diagnostic-uninitialized]
1 [clang-diagnostic-unused-variable]
1 [modernize-use-nullptr]
```

## Disable one checker ✅

```sh
CodeChecker analyze compile_commands.json -o reports -e profile:extreme -d core.DivideZero
```

No `core.DivideZero` report appears, even though the `extreme` profile
includes it.

## Run only the checkers you list ✅

```sh
CodeChecker analyze compile_commands.json -o reports --disable-all -e core.DivideZero
```

```
1 [core.DivideZero]
```

`--disable-all` is the same as giving `--disable default` first.

## Enable (almost) every checker ✅

```sh
CodeChecker analyze compile_commands.json -o reports --enable-all
```

```
6 [clang-diagnostic-missing-prototypes]
1 [clang-diagnostic-uninitialized]
...
11 [readability-identifier-length]
4 [readability-magic-numbers]
```

These groups are still **not** enabled and must be listed with `-e`:
`alpha.`, `debug.`, `osx.`, `abseil-`, `android-`, `darwin-`, `objc-`,
`cppcoreguidelines-`, `fuchsia.`, `fuchsia-`, `hicpp-`, `llvm-`, `llvmlibc-`,
`google-`, `zircon-`.

## Enable a checker prefix group ✅

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy -e prefix:modernize
```

```
1 [modernize-use-nullptr]
3 [modernize-use-trailing-return-type]
...
```

`-e modernize` also works when the name is not ambiguous. For Clang SA,
packages work the same way: `-e core`, `-e core.uninitialized`.

## Enable a profile ✅

```sh
CodeChecker analyze compile_commands.json -o reports -e profile:extreme
CodeChecker analyze compile_commands.json -o reports -e sensitive
```

Profiles: `default`, `sensitive`, `security`, `portability`, `extreme`.

## Enable the checkers of a guideline ✅

```sh
CodeChecker analyze compile_commands.json -o reports -e guideline:sei-cert-c
```

```
1 [clang-diagnostic-uninitialized]
1 [clang-diagnostic-unused-variable]
1 [core.DivideZero]
1 [gcc-analyzer-use-of-uninitialized-value]
```

This also turned on a GCC checker, so GCC ran as well.

## Enable by any label ✅

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy -e severity:STYLE
```

Any `label:value` shown by `CodeChecker checkers --label` works.

## Name clashes

If a name is both a group and a profile or guideline, add the namespace:
`checker:`, `prefix:`, `profile:` or `guideline:`. If a checker name is also
the prefix of other checkers, `checker:` or `prefix:` is required.

## Compiler warnings ⚠️

Clang-Tidy reports compiler warnings as `clang-diagnostic-<warning>` checkers.
In 6.30.0, use that full name to turn them on or off:

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy -e clang-diagnostic-unused-variable
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy -d clang-diagnostic-unused-variable
```

⚠️ The help text says warnings are enabled by their original name, e.g.
`-e Wunused-variable`. In 6.30.0 this fails:

```
[ERROR] - No checker(s) with these names was found: Wunused-variable.
```

GCC-compatible compiler errors are reported as `clang-diagnostic-error`.

## Unknown checker names ✅

```sh
CodeChecker analyze compile_commands.json -o reports -e no-such-checker
```

```
[ERROR] - No checker(s) with these names was found: no-such-checker.
[INFO] - Although it is not recommended, if you want to suppress errors relating to unknown checker names, consider using the option '--no-missing-checker-error'
```

Exit code `1`. With `--no-missing-checker-error`, this becomes a warning and
the analysis runs (exit code `0`). The same check applies to names in
`--analyzer-config` and `--checker-config`.

## Where the final list is saved

The checkers actually enabled for each analyzer are saved in
`reports/metadata.json`.
