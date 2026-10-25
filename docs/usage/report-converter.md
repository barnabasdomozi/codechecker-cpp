# Convert other tools' output (`report-converter`)

`report-converter` turns the output of other analyzers, sanitizers and linters
into a CodeChecker report folder. The folder can then be used with `parse`,
`store`, `cmd diff` and so on, like one from `analyze`.

```
report-converter -t TYPE -o OUTPUT_DIR [-e {plist,sarif}] [--meta KEY=VALUE ...]
                 [--filename PATTERN] [-c] [-v] INPUT [INPUT ...]
report-converter --example TYPE
```

`report-converter` is in the `bin/` folder of the CodeChecker package.

## Supported types

| Type | Tool | Tested |
|---|---|---|
| `asan` | AddressSanitizer | ✅ |
| `ubsan` | UndefinedBehaviorSanitizer | ✅ |
| `msan` | MemorySanitizer | ❌ |
| `tsan` | ThreadSanitizer | ❌ |
| `lsan` | LeakSanitizer | ✅ |
| `clang-tidy` | Clang-Tidy text output | ✅ |
| `clang-tidy-yaml` | Clang-Tidy `-export-fixes` YAML | ✅ |
| `gcc` | GCC `-fanalyzer` SARIF | ✅ |
| `cppcheck` | Cppcheck plist | ✅ |
| `fbinfer` | Facebook Infer | ❌ |
| `pylint` | Pylint JSON | ✅ |
| `pyflakes` | Pyflakes | ❌ |
| `ruff` | ruff | ❌ |
| `cpplint` | cpplint | ❌ |
| `coccinelle`, `smatch`, `sparse`, `kernel-doc` | Linux kernel tools | ❌ |
| `eslint`, `tslint`, `jscpd` | JavaScript/TypeScript tools | ❌ |
| `golint` | Go | ❌ |
| `pmd`, `spotbugs` | Java | ❌ |
| `roslynator` | C# | ❌ |
| `pvs-studio` | PVS-Studio | ❌ |
| `mdl` | Markdownlint | ❌ |
| `sphinx` | Sphinx | ❌ |

❌ means the tool was not installed on the test machine.

## Show how to produce input for a type ✅

```sh
report-converter --example clang-tidy
report-converter --example gcc
report-converter --example asan
```

```
report-converter -t clang-tidy -o ./codechecker_clang_tidy_reports     ./clang-tidy_reports.out

# Store the clang-tidy reports with CodeChecker.
CodeChecker store ./codechecker_clang_tidy_reports -n clang-tidy
```

```
report-converter -t gcc -o ./codechecker_gcc_reports my_file.cpp.sarif
```

## Clang-Tidy ✅

```sh
clang-tidy main.c -checks='-*,clang-analyzer-*,bugprone-*' -- -I. > tidy.out
report-converter -t clang-tidy -o ./rc_clang-tidy tidy.out

clang-tidy main.c -checks='-*,clang-analyzer-*' -export-fixes=fixes.yaml --
report-converter -t clang-tidy-yaml -o ./rc_clang-tidy-yaml fixes.yaml
```

```
[INFO] - Create/modify plist file: './rc_clang-tidy/main.c_clang-tidy_610112597e9096a39e5a0f2a13fe72b6.plist'.
[WARNING] - Use '--meta' option to provide extra information to the CodeChecker server such as analyzer version and analysis command when storing the results to it. For more information see the --help.
```

## GCC static analyzer ✅

```sh
gcc -fanalyzer -fdiagnostics-format=sarif-file -c main.c -o /dev/null    # writes main.c.sarif
report-converter -t gcc -o ./rc_gcc main.c.sarif
```

## Sanitizers ✅

```sh
clang -fsanitize=address -g asan.c -o asan_bin && ./asan_bin 2> asan.out
report-converter -t asan -o ./rc_asan asan.out
CodeChecker parse ./rc_asan
```

```
[HIGH] ~/rc/asan.c:2:57: heap-buffer-overflow on address 0x502000000024 at pc 0x556e547368d6 bp 0x7fffa9c306d0 sp 0x7fffa9c306c8 [AddressSanitizer.generic-error]
```

```sh
clang -fsanitize=undefined -g ubsan.c -o ubsan_bin && ./ubsan_bin 2> ubsan.out
report-converter -t ubsan -o ./rc_ubsan ubsan.out
```

```sh
clang -fsanitize=leak -g leak.c -o leak_bin && ./leak_bin 2> leak.out
report-converter -t lsan -o ./rc_lsan leak.out
```

LeakSanitizer reports become `LeakSanitizer` reports (severity HIGH). The
memory addresses in the sanitizer messages change from run to run, but the
report hashes do not (verified with
[blackbox_tests/analyze-targets/external-tools](../../blackbox_tests/analyze-targets/external-tools/README.md)).

## Cppcheck ✅

```sh
cppcheck --plist-output=./cppcheck_out $PWD/main.c
report-converter -t cppcheck -o ./rc_cppcheck ./cppcheck_out
```

The checker names are Cppcheck's own (`zerodiv`, `uninitvar`), without the
`cppcheck-` prefix that `CodeChecker analyze` gives them, and the severity
is `UNSPECIFIED`. Give Cppcheck absolute paths (as
`report-converter --example cppcheck` says): a relative path in the plist
is resolved against the plist folder, so the report points to e.g.
`cppcheck_out/main.c`.

## Pylint ✅

```sh
python3 -m pylint -f json bad.py > pylint.json
report-converter -t pylint -o ./rc_pylint pylint.json
CodeChecker parse ./rc_pylint
```

```
[LOW] ~/rc/bad.py:1:0: Missing module docstring [missing-module-docstring]
[MEDIUM] ~/rc/bad.py:1:0: Unused import os [unused-import]
[LOW] ~/rc/bad.py:2:0: Missing function or method docstring [missing-function-docstring]
...
```

## Output format ✅

```sh
report-converter -t clang-tidy -o ./rc_sarif -e sarif tidy.out
```

Writes `.sarif` files instead of `.plist` (`-e plist` is the default).
`CodeChecker parse` reads the SARIF folder too.

## Metadata, file names, cleaning ✅

```sh
report-converter -t clang-tidy -o ./rc_meta \
  --meta analyzer_command="clang-tidy main.c" analyzer_version=19.1.7 \
  --filename "{source_file}_{analyzer}_custom" tidy.out
```

```
rc_meta/
├── main.c_clang-tidy_custom.plist
└── metadata.json     {"version": 2, "num_of_report_dir": 1,
                       "tools": [{"name": "clang-tidy", "version": "19.1.7", "command": "clang-tidy main.c"}]}
```

- `--meta`: `analyzer_command` and `analyzer_version` are the valid keys.
  The server shows them with the run.
- `--filename`: name pattern for result files. Placeholders: `{source_file}`,
  `{analyzer}`, `{file_hash}`. Default:
  `{source_file}_{analyzer}_{file_hash}`.
- `-c`: delete old files in the output folder first.
- `-v`: debug output, including every parsed report as JSON.

## Input without matching results ✅

```sh
report-converter -t cppcheck -o ./rc_x tidy.out
```

```
[INFO] - No 'cppcheck' results can be found in 'tidy.out'.
```

Exit code `0`.

## Store converted results ✅

```sh
CodeChecker store ./rc_asan -n asan_run --url localhost:8555/Default
```
