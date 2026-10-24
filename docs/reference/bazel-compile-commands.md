# Reference: `bazel-compile-commands`

Usage: [../usage/bazel-compile-commands.md](../usage/bazel-compile-commands.md)

Python source: `tools/bazel/bazel_compile_commands/bazel_compile_commands.py`
(installed as `bin/bazel-compile-commands`).

Verified with a fake `bazel` script answering `info` and `aquery` (no real
Bazel was available).

## 1. Command line

```
bazel-compile-commands [-o/--output FILE] [-b/--build "BAZEL BUILD COMMAND"] [-v ...]
```

Plain `argparse`. Defaults: `-o compile_commands.json`. Verbosity starts at
1; each `-v` adds one: 1 → INFO, ≥ 2 → DEBUG. Log format
`[bazel] %(levelname)5s: %(message)s` on stderr.

Exit 1 if the run fails (§2), else 0.

## 2. Algorithm

1. No `-b` → ERROR `No bazel build command specified`, exit 1. Empty `-o`
   → ERROR `No output file is specified`, exit 1.
2. `bazel` = the first word of `-b` (shell-split). Run `<bazel> info
   workspace`, `<bazel> info output_base`, `<bazel> info execution_root`
   (stdout captured and right-stripped; a failing command logs ERROR
   `Command: <cmd>...\nError: <stderr bytes>`; a missing program raises
   `FileNotFoundError`, exit 1).
3. Work directory = execution root with `/execroot/` replaced by
   `/external/`; if that does not exist, the workspace. INFO lines
   `Bazel workspace:     <ws>`, `Bazel output base:   <ob>`,
   `Bazel work dir:      <wd>`, `Bazel build options: <-b>`.
4. Query (`bazel_aquery`): from the shell-split `-b` arguments, targets =
   arguments matching `^(@|:|//)\S*` or equal to `...`; configs = arguments
   starting with `--`. Other arguments (the program name, `build`, short
   options like `-k`) are dropped. No targets → ERROR `No targets found for
   bazel query in: <-b>` and failure. Command (a string, shell-split again):

   ```
   <bazel> aquery --output=jsonproto --include_artifacts=false 'mnemonic("CppCompile", deps("<t1>") + deps("<t2>"))' <configs joined by space>
   ```

   Its stdout is parsed as JSON. Empty output → ERROR `Command '<-b>'`,
   ERROR `Produces no output`, exit 1.
5. Search directories = `[output_base, work dir]`. For each action with
   `mnemonic == "CppCompile"`:
   - no `-c` in `arguments` → WARNING `Wrong compile command: <list>`;
   - file = the argument after `-c`; ending with `.asm` → INFO `Skipping:
     <file>`;
   - the first search directory where `<dir>/<file>` is a regular file
     gives `{"file": "<dir>/<file>", "directory": "<dir>", "command":
     <arguments joined by single spaces>}` (no quoting; INFO `Add file:
     <file>`); not found → WARNING `Cannot find: <file>`;
   - command filtering (regex replacements on the joined string):
     `( |=)bazel-out/\S*/bin/external/` → `\1<output_base>/external/`;
     ` -MD ` → ` `; ` -MF \S* ` → ` `; ` -MT \S* ` → ` `.
   INFO `<n> files added, <s> skipped (asm), <m> not found`.
6. INFO `Saving to: <file>`; write the list as JSON indented by 4.

Note: because arguments are joined without quoting, arguments containing
spaces or shell metacharacters break the resulting `command`.
