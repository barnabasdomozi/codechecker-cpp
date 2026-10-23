# List and apply automatic fixes (`CodeChecker fixit`)

Some Clang-Tidy checkers suggest fixes ("fix-its"), for example
`modernize-use-nullptr` replaces `0` with `nullptr`. During `analyze`, these
are saved as YAML files in `reports/fixit/`. `CodeChecker fixit` lists them
and can apply them to the sources.

```
CodeChecker fixit REPORT_FOLDER [...] [-a | -i]
                  [--checker-name PATTERN ...] [--file PATTERN ...]
```

**Requirement:** `clang-apply-replacements` must be on `PATH`. Some
distributions install it only with a version suffix
(`clang-apply-replacements-19`) or under `/usr/lib/llvm-19/bin`. ⚠️ If it is
missing, `fixit` prints the following and **exits with code 0**:

```
[ERROR] - clang-apply-replacements tool is not found
```

Fix: `export PATH=$PATH:/usr/lib/llvm-19/bin`.

Setup used below:

```sh
CodeChecker analyze compile_commands.json -o reports --analyzers clang-tidy \
  -e modernize-use-nullptr -e modernize-use-trailing-return-type
```

## List all fixes ✅

```sh
CodeChecker fixit reports
```

The fixes are printed in `clang-apply-replacements` YAML format:

```yaml
- BuildDirectory: ~/demo
  DiagnosticMessage:
    FileOffset: '239'
    FilePath: ~/demo/style.cpp
    Message: use a trailing return type for this function
    Replacements:
    - FilePath: ~/demo/style.cpp
      Length: '3'
      Offset: '235'
      ReplacementText: auto
    - FilePath: ~/demo/style.cpp
      Length: '0'
      Offset: '255'
      ReplacementText: ' -> int'
  DiagnosticName: modernize-use-trailing-return-type
  Level: Warning
MainSourceFile: ~/demo/style.cpp
```

## Filter by checker or file ✅

```sh
CodeChecker fixit reports --checker-name "*nullptr" --file "*/style.cpp"
```

```yaml
    - FilePath: ~/demo/style.cpp
      Length: '1'
      Offset: '49'
      ReplacementText: nullptr
  DiagnosticName: modernize-use-nullptr
  ...
```

Both options accept several patterns with `*`.

## Apply fixes ✅

```sh
CodeChecker fixit reports -a --checker-name "*nullptr"
```

```
Updated files:
~/demo/style.cpp
```

`style.cpp` line 4 changes from `int *p = 0;` to `int *p = nullptr;`.

Files changed since the analysis are not touched. Running the same command
again prints:

```
Skipped files due to modification since last analysis:
~/demo/style.cpp
```

## Choose fixes one by one ✅

```sh
CodeChecker fixit reports -i --checker-name "*nullptr"
```

Each fix is shown, followed by a prompt:

```
    ReplacementText: nullptr
DiagnosticName: modernize-use-nullptr
Level: Warning

y/<Enter> - Yes
Other     - No
Ctrl-C    - Cancel
Apply: n
```

Interactive mode needs a terminal. When standard input is not a terminal:

```
[ERROR] - Interactive mode can't be used in case of JSON data arriving from standard input.
```
(exit code 1)

## Fix only new reports (pipe from `cmd diff`) ⚠️

`fixit` reads JSON from standard input when it is not a terminal, and applies
only the fixes for the reports in that JSON:

```sh
CodeChecker cmd diff -b my_run -n ./reports -o json --new --url localhost:8001/Default \
  | CodeChecker fixit ./reports
```

✅ This works when the base (`-b`) is a **server run** and the new side (`-n`)
is a **local report folder**.

⚠️ When both sides are local folders, or both are server runs, `fixit`
crashes:

```
KeyError: 'checkedFile'
```

(exit code 1). The JSON from these diffs uses a different report format. See
[known-issues.md](known-issues.md).

## Exit codes

`0` on success, including when `clang-apply-replacements` is missing. `1` on
errors such as the interactive mode or pipe errors above.
