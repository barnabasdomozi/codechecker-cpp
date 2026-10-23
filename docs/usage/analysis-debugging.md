# Debug analyses: captured output, reproducers, failures, timeouts

## Keep the analyzers' output for successful runs ✅

```sh
CodeChecker analyze compile_commands.json -o reports --capture-analysis-output
```

The standard output and error of each successful analyzer run are saved in
`reports/success/`:

```
main.c_clangsa_681db965dea6200719ba62bde2ba8cb4.plist.stderr.txt
main.c_clang-tidy_681db965dea6200719ba62bde2ba8cb4.plist.stderr.txt
main.c_clang-tidy_681db965dea6200719ba62bde2ba8cb4.plist.stdout.txt
...
```

## Generate reproducers ✅

```sh
CodeChecker analyze compile_commands.json -o reports --generate-reproducer
```

For every analyzer action, a zip file with everything needed to repeat it
(the analyzer command, sources and headers) is written to
`reports/reproducer/`:

```
reports/reproducer/
├── main.c_clangsa_681db965dea6200719ba62bde2ba8cb4.plist.zip
├── main.c_clang-tidy_681db965dea6200719ba62bde2ba8cb4.plist.zip
└── ...
```

With this flag, `reports/failed/` stays empty.

## Failed analyses ✅

If an analyzer fails on a file (for example a compile error), CodeChecker
saves a zip file with the command, the output and the sources in
`reports/failed/`, and exits with code `3`.

```sh
echo 'int broken( {' > broken.c
CodeChecker log -b "gcc -c broken.c -o broken.o" -o cc.json
CodeChecker analyze cc.json -o reports --analyzers clangsa      # exit code 3
ls reports/failed
```

```
broken.c_clangsa_7dfa2ae3fced10280254e220f86ce1af.plist_compile_error.zip
```

`CodeChecker parse reports --status` lists the failures, and `--detailed`
adds the file names (see [parse.md](parse.md)):

```
[INFO] - Failed to analyze
[INFO] -   clangsa: ['~/brk/broken.c'] (1)
[INFO] - Missing analysis results
[INFO] -   clang-tidy: ['~/brk/broken.c'] (1)
```

## Time limit per analysis ✅

```sh
CodeChecker analyze compile_commands.json -o reports --timeout 60
```

If one analyzer run on one file takes longer than the limit (in seconds), it
is killed and counted as failed.

## Debug logging ✅

```sh
CodeChecker analyze compile_commands.json -o reports --verbose debug
CodeChecker analyze compile_commands.json -o reports --verbose debug_analyzer
```

`debug` shows CodeChecker internals with source file and line.
`debug_analyzer` also shows the analyzer commands and their output.
`--verbose` works on every command.
