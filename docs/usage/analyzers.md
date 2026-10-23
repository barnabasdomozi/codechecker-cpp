# List analyzers and their options

`CodeChecker analyzers` shows which analyzer tools CodeChecker supports, which
of them it found, their binary paths and versions. It also lists the
configuration options each analyzer accepts.

Supported analyzers: `clangsa` (Clang Static Analyzer), `clang-tidy`,
`cppcheck`, `gcc` (GCC `-fanalyzer`), `infer` (Facebook Infer).

```
CodeChecker analyzers [--dump-config ANALYZER] [--analyzer-config ANALYZER]
                      [-o {rows,table,csv,json}] [--all] [--details]
```

## List available analyzers ✅

```sh
CodeChecker analyzers
```

```
[WARNING] - Can't analyze with 'infer': Failed to detect analyzer binary!
[WARNING] - Can't analyze with 'cppcheck': Failed to detect analyzer binary!
 clangsa /usr/lib/llvm-19/bin/clang       19.1.7
 clang-tidy /usr/lib/llvm-19/bin/clang-tidy  19.1.7
 cppcheck                                  NOT FOUND
 gcc /usr/bin/x86_64-linux-gnu-g++-14 14.2.0
 infer                                  NOT FOUND
```

Analyzers that are not found are still listed, with `NOT FOUND`, and a
warning is printed for each.

## Other output formats ✅

```sh
CodeChecker analyzers -o table
CodeChecker analyzers -o csv
CodeChecker analyzers -o json
```

```
name,path,version
clangsa,/usr/lib/llvm-19/bin/clang,19.1.7
clang-tidy,/usr/lib/llvm-19/bin/clang-tidy,19.1.7
cppcheck,,NOT FOUND
...
```

```json
[{"name": "clangsa", "path": "/usr/lib/llvm-19/bin/clang", "version": "19.1.7"}, ...]
```

`--all` and `--details` are deprecated and have no effect; the output is the
same as without them ✅.

## Show the configuration options of an analyzer ✅

The options printed here can be passed to
`CodeChecker analyze --analyzer-config <analyzer>:<option>=<value>`
(see [analyzer-configuration.md](analyzer-configuration.md)).

```sh
CodeChecker analyzers --analyzer-config clangsa
CodeChecker analyzers --analyzer-config clang-tidy
CodeChecker analyzers --analyzer-config gcc
CodeChecker analyzers --analyzer-config cppcheck     # works even if cppcheck is not installed
CodeChecker analyzers --analyzer-config clang-tidy -o json
```

```
 clangsa:cc-verbatim-args-file A file path containing flags that are forwarded verbatim to the analyzer tool. ...
 clangsa:add-pop-up-notes (bool) Whether pop-up notes should be added to the final output. (default: true)
 clangsa:aggressive-binary-operation-simplification (bool) ... (default: false)
 ...
```

```
 clang-tidy:Checks 'clang-diagnostic-*,clang-analyzer-*,*'
 clang-tidy:WarningsAsErrors ''
 clang-tidy:HeaderFilterRegex ''
 clang-tidy:ExcludeHeaderFilterRegex ''
 clang-tidy:FormatStyle none
 clang-tidy:User user
 clang-tidy:SystemHeaders false
 clang-tidy:cc-verbatim-args-file A file path containing flags ...
```

```
 cppcheck:addons A list of cppcheck addon files.
 cppcheck:libraries A list of cppcheck library definiton files.
 cppcheck:platform The platform configuration .xml file.
 cppcheck:inconclusive Enable inconclusive reports.
 cppcheck:cc-verbatim-args-file ...
```

Every analyzer has the CodeChecker-specific `cc-verbatim-args-file` option.
Clang-Tidy also has `take-config-from-directory`, which is not listed here
(see [analyzer-configuration.md](analyzer-configuration.md)).

## Dump a full analyzer configuration ✅ / ⚠️

```sh
CodeChecker analyzers --dump-config clang-tidy > .clang-tidy
```

```yaml
---
Checks:          'clang-diagnostic-*,clang-analyzer-*,*'
WarningsAsErrors: ''
HeaderFileExtensions:
  - ''
  - h
  ...
```

The help text says only Clang-Tidy supports `--dump-config`. ⚠️ In practice,
`--dump-config clangsa` prints Clang's own analyzer option help
(`OVERVIEW: Clang Static Analyzer Checker and Package Option List ...`)
and exits `0`.

A `.clang-tidy` file placed in the project is used by `analyze` and `check`
only together with `--analyzer-config clang-tidy:take-config-from-directory=true`.

## Choose which analyzer binaries are used ✅

These environment variables affect every command that looks up analyzers
(`analyzers`, `checkers`, `analyze`, `check`):

```sh
# Take analyzers from PATH instead of the configured binaries
CC_ANALYZERS_FROM_PATH=1 CodeChecker analyzers

# Give exact binary paths, separated by ';'
CC_ANALYZER_BIN="clangsa:/usr/lib/llvm-19/bin/clang;clang-tidy:/usr/lib/llvm-19/bin/clang-tidy" \
  CodeChecker analyzers
```

`CC_CLANGSA_PLUGIN_DIR` sets the Clang Static Analyzer plugin directory when
`CC_ANALYZERS_FROM_PATH` is used. ❌ Not tested (no plugins available).

The default binaries come from `config/package_layout.json` in the
CodeChecker installation.

Exit code: `0`.
