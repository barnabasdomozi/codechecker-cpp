# Reference: cross translation unit (CTU) analysis

Usage: [../usage/ctu-analysis.md](../usage/ctu-analysis.md)

Python source: `analyzer/codechecker_analyzer/analyzers/clangsa/ctu_manager.py`,
`ctu_triple_arch.py`, `analyzer.py` (CTU parts), `pre_analysis_manager.py`,
`analyzer.py` (`perform_analysis`), `analysis_manager.py`,
`analyzer/tools/merge_clang_extdef_mappings`.

## 1. Options → state

| Option | Phases `[collect, analyze]` |
|---|---|
| `--ctu` / `--ctu-all` | `[true, true]` |
| `--ctu-collect` | `[true, false]` |
| `--ctu-analyze` | `[false, true]` |

- CTU dir: `<out>/ctu-dir`.
- On-demand mode: **only** if `--ctu-ast-mode parse-on-demand` is given.
  Without `--ctu-ast-mode` the AST-dump mode is used (contrary to the help
  text).
- `--compile-uniqueing` `none` becomes `alpha`.
- `--ctu-ast-mode` without a phase option → error, exit 1.

## 2. Capability checks

- **CTU capable**: the mapping tool exists and `<tool> -version` succeeds.
  Mapping tool: for Clang major > 7, `clang-extdef-mapping` (map file
  `externalDefMap.txt`), else `clang-func-mapping` (`externalFnMap.txt`).
  Looked up next to the analyzer clang binary, then with `-<major>` suffix
  (e.g. `clang-extdef-mapping-19`).
- **On-demand capable**: `<clang> -cc1 -analyzer-config-help` contains
  `ctu-invocation-list`.

## 3. Target architecture ("triple arch")

Per action and source, run the compile command (below) with `-###` inserted
after the binary, in the action's directory; in its stdout+stderr split on
whitespace (quotes stripped), take the token after `-triple` and keep the
part before the first `-` (e.g. `x86_64`). Not found → empty string.

Compile command used for CTU and statistics:

```
<clang> [--target=<target>] -c [-x <lang>] <verbatim args> <analyzer_options>
        <implicit include flag> <dir>... [-o <output>] [<source>] [<compiler_standard> if no -std/--std]
```

## 4. Collect phase (pre-analysis)

Only for `clangsa` jobs, in a pool of `-j` processes. INFO
`Pre-analysis started.`, `Collecting data for ctu analysis.`; per action INFO
`[<n>/<total>] <basename>` (skipped actions are counted but not processed).

For each action:
1. Compute the triple arch `A`.
2. **AST-dump mode**: run
   `<compile command> -emit-ast -D__clang_analyzer__ -w -o <ctu-dir>/A/ast/<realpath(source) without leading '/'>.ast`
   (creating the directory). Non-zero exit → ERROR
   `Error generating AST.\n\ncommand:\n\n<cmd>\n\nstderr:\n\n<err>`.
   **On-demand mode**: append one YAML mapping line
   `<absolute resolved source>: [<compile command> + -D__clang_analyzer__ -w]`
   to `<ctu-dir>/A/invocation-list.yml` (flow-style list on one line, no line
   wrapping, keys always emitted as simple keys; appended under a file
   append, no locking).
3. **Function map**: run
   `<mapping tool> <source> -- <compile command without source>` (cwd = action
   directory). Non-zero → ERROR `Error generating function map. ...` and stop
   for this action. For each output line:
   - new format (line starts with a digit): `<len>:<usr> <path>`; the USR is
     exactly `len` characters after the colon; the path follows after one
     space;
   - old format: `<usr> <path>` split at the first space;
   - path mapping: AST-dump mode → `ast/<path without leading '/'>.ast`;
     on-demand → path unchanged.
   Write the mapped lines (`<usr-with-length-prefix> <mapped path>`) to a new
   uniquely named temporary file in `<ctu-dir>/A/tmpExternalFnMaps/`.
4. Exceptions are logged (`Pre-analysis failed for <source>: <e>`) and do not
   stop other actions.

After all actions, for every directory `A` in `ctu-dir`: merge
`A/tmpExternalFnMaps/*` into `A/externalDefMap.txt` (see
[merge-clang-extdef-mappings.md](merge-clang-extdef-mappings.md)) and delete
the temp folder. INFO `Pre-analysis finished.`

## 5. Analyze phase

clangsa command gets the CTU block ([analyzer-drivers.md](analyzer-drivers.md)
§5) with `ctu-dir=<out>/ctu-dir/<triple arch of this action>`.

Mentioned files: lines `CTU loaded AST file: <path>` in the analyzer output.
`<path>` is joined to `<ctu-dir>/<arch>`; if it contains `.ast`, it is
mapped back to the original source by stripping the `<ctu-dir>/<arch>/ast`
prefix and the `.ast` suffix (error logged if the prefix is not there);
otherwise (on-demand) the joined path is used as is. These files go to
`<out>/ctu_connections/<result name>` and into reproducer zips.

`--ctu-reanalyze-on-failure`: see [analyze.md](analyze.md) §4.1.

## 6. Phase control in `perform_analysis`

| Situation | Behavior |
|---|---|
| `clangsa` not among usable analyzers | ERROR `CTU can only be used with the clang static analyzer.`, nothing analyzed |
| collect phase requested | `ctu-dir` deleted before parsing and again before pre-analysis |
| analyze only, `ctu-dir` missing | ERROR `CTU directory: '<d>' does not exist.`, nothing analyzed |
| `--ctu-collect` | after pre-analysis, the analysis step is skipped (only pre-analysis runs) |
| `--ctu` | after analysis, `ctu-dir` is deleted |

Skipping: with `--ctu-collect` alone, skipped files are not pre-analyzed;
with `--ctu`/`--ctu-analyze`, all files are pre-analyzed/kept in parsing and
skipped files are only left out of the final analysis.

## 7. Makefile mode

See [makefile-mode.md](makefile-mode.md): the same commands are written as
Makefile recipes (AST-dump mode only; the map file paths are rewritten with
`sed -E "s|/(.*)|ast/\1.ast|"`).
