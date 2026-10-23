# Compilation database from Bazel (`bazel-compile-commands`)

Bazel builds run in sandboxes, so `CodeChecker log` cannot record them.
`bazel-compile-commands` asks Bazel for the compile actions with
`bazel aquery` and writes a `compile_commands.json` without building.

```
bazel-compile-commands [-o OUTPUT] [-b BUILD] [-v]
```

- `-o`: output file (e.g. `compile_commands.json`).
- `-b`: the **full** Bazel build command, e.g. `"bazel build //..."`. The
  tool turns it into an `aquery` call.
- `-v`: verbose logging.

## Example ❌

```sh
bazel-compile-commands -b "bazel build //main:app" -o compile_commands.json
CodeChecker analyze compile_commands.json -o reports
```

Not verified: Bazel is not installed on the test machine. Observed:

- `-b "bazel build //..."` without Bazel installed:
  `FileNotFoundError: [Errno 2] No such file or directory: 'bazel'` (exit 1).
- `-b "//..."`: the first word is run as the program, so it fails with
  `FileNotFoundError: ... '//...'`. This confirms that `-b` takes the whole
  command line.
