# Compiler include paths, target and language standard

Projects are often built with GCC, but analyzed with Clang. Clang does not
know GCC's built-in include folders, default language standard or target
triple. So before analysis, CodeChecker runs each compiler from the
compilation database once to ask for these values, and adds them to the
analyzer command. The results are saved in `reports/compiler_info.json`.

Options (on both `analyze` and `check`):

```
--dump-compiler-info-file
--compiler-info-file FILE
--keep-gcc-include-fixed
--keep-gcc-intrin
--add-gcc-include-dirs-with-isystem
```

## Save the detected compiler info ✅

```sh
CodeChecker analyze compile_commands.json -o compiler_info.json --dump-compiler-info-file
```

With this flag, **`-o` is the output JSON file**, and no analysis runs.

```json
{"[\"/usr/bin/gcc\", \"c\", []]": {
   "compiler_includes": ["/usr/lib/gcc/x86_64-linux-gnu/14/include", "/usr/local/include",
                         "/usr/include/x86_64-linux-gnu", "/usr/include"],
   "compiler_standard": "-std=gnu17",
   "target": "x86_64-linux-gnu"},
 "[\"/usr/bin/g++\", \"c++\", [\"-std=c++17\"]]": {
   "compiler_includes": ["/usr/include/c++/14", "/usr/include/x86_64-linux-gnu/c++/14", ...],
   ...}}
```

The key is `[compiler, language, flags that change the result]`.

## Use a saved compiler info file ✅

```sh
CodeChecker analyze compile_commands.json -o reports --compiler-info-file compiler_info.json
```

CodeChecker reads includes, target and standard from the file instead of
running the compilers. Use this when the compilers are not available where the
analysis runs (e.g. cross-compilers), or to adjust the values by hand.

If the file does not exist:

```
[ERROR] - Compiler info file compiler_info.json does not exist
```
(exit code 1)

## Keep GCC-only include folders ✅

```sh
CodeChecker analyze compile_commands.json -o reports --keep-gcc-include-fixed --keep-gcc-intrin
```

- `--keep-gcc-include-fixed`: keep GCC's `include-fixed` folders. These are
  removed by default.
- `--keep-gcc-intrin`: keep folders with GCC `*intrin.h` headers. These are
  removed by default. Try this when Clang fails with errors about
  `__builtin` symbols.

## Use `-isystem` instead of `-idirafter` ✅

```sh
CodeChecker analyze compile_commands.json -o reports --add-gcc-include-dirs-with-isystem
```

Since CodeChecker 6.24.1, implicit include folders are added with
`-idirafter`. This flag goes back to the old `-isystem` behavior.
