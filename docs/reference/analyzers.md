# Reference: `CodeChecker analyzers`

Usage: [../usage/analyzers.md](../usage/analyzers.md)

Python source: `analyzer/codechecker_analyzer/cli/analyzers.py`,
`analyzer_context.py`, `env.py`, `host_check.py`,
`analyzers/analyzer_types.py`, `analyzers/*/analyzer.py`.

## 1. Analyzer registry

Five analyzers, in this fixed order (the order of every listing):

| Name | Default binary (`package_layout.json`) | Version query | Minimum version |
|---|---|---|---|
| `clangsa` | `clang` | `<bin> -dumpversion`, strip trailing `git` | none |
| `clang-tidy` | `clang-tidy` | `<bin> --version`, regex `.*version (?P<v>[\d.]+)` (dot-all) | none |
| `cppcheck` | `cppcheck` | `<bin> --version`, regex `^Cppcheck(.*?)(?P<v>[\d.]+)`, append `.0` if only `X.Y` | 1.80 |
| `gcc` | `g++` | `<bin> -dumpfullversion` | 13.0.0 (no version → "too old to support -dumpfullversion") |
| `infer` | `infer` | `<bin> --version`, first line, last word without its first character (`v1.1.0` → `1.1.0`); run with `TZ=UTC` | none |

Versions are parsed as semantic versions (`major.minor.patch`).

## 2. Binary resolution (done once per process)

For each analyzer, in order of priority:

1. **`CC_ANALYZER_BIN`**: `analyzer:path;analyzer2:path2`. Each item is split
   with regex `(.+):(.+)` (greedy, so the split is at the **last** colon).
   `path` is resolved with a `PATH` lookup (`which`); if the result is not a
   file → error `'<path>' is not a path to an analyzer binary given to CC_ANALYZER_BIN!`.
   Bad items log the error and, after all items, print the expected format and
   **exit 1**. Good items log `Using '<path>' for analyzer '<name>'`.
2. If `CC_ANALYZERS_FROM_PATH` is `yes` or `1` (case-insensitive): use only
   the base name of the configured value and look it up in `PATH`.
3. Otherwise the configured value: if it has a directory part, it is relative
   to the package root; else look it up in `PATH`.
4. A found binary is canonicalized (symlinks resolved), **except** when the
   resolved target ends in `/ccache`: then the unresolved path is kept.
5. Not found → no binary (`None`).

The `clang-apply-replacements` binary (used by `fixit`) is resolved the same
way (step 3 only).

## 3. Availability check (`check_supported_analyzers`)

For each requested analyzer name:

1. Unknown name → failed, reason `Analyzer unsupported by CodeChecker!`.
2. No binary → failed, `Failed to detect analyzer binary!`.
3. Relative binary path → the analyzer's `resolve_missing_binary` searches
   `PATH` for versioned names and picks one (see below). If the found name
   differs from the configured one, it replaces the configured binary.
4. Version check (`is_binary_version_incompatible`) → failed,
   `Incompatible version: <msg> Maybe try setting an absolute path to a different analyzer binary via the env variable CC_ANALYZER_BIN?`.
5. Run `<bin> --version`; non-zero exit or not executable → failed,
   `Cannot execute analyzer binary!`.
6. Otherwise enabled.

Returns `(enabled set, failed set of (name, reason))`.

`resolve_missing_binary` (clangsa, clang-tidy, cppcheck only): scan every
`PATH` directory for files matching
- clangsa: `^clang(\+\+)?(-\d+(\.\d+){0,2})?$` (preferred names `clang`, `clang++`)
- clang-tidy: `^clang-tidy(-\d+(\.\d+){0,2})?$` (preferred `clang-tidy`)
- cppcheck: `^cppcheck(-\d+(\.\d+){0,2})?$` (preferred `cppcheck`)

If only one name matches, take its first occurrence in `PATH`. With several
names: a preferred name if present, else the alphabetically last name
("newest"); take the first occurrence of it in `PATH`.

## 4. Process environment for tools

`get_env_for_bin(binary)`: if the resolved binary is inside the package root,
use the current environment extended with `path_env_extra` /
`ld_lib_path_extra` from `package_layout.json` (prepended to `PATH` /
`LD_LIBRARY_PATH`; skipped when `CC_ANALYZERS_FROM_PATH` is set). Otherwise
use the **original** environment saved by the launcher
([common-infrastructure.md](common-infrastructure.md) §2). If the binary does
not exist → log `Binary <x> not found` and return no environment.

## 5. Command behavior

Logging goes to stderr unless `-o table`.

1. Run the availability check for all five analyzers (remember failures).
2. `--dump-config <analyzer>`:
   - `clang-tidy`: run `<bin> -dump-config -checks=*` with output passed
     through to stdout.
   - `clangsa`: run `<bin> -cc1 -analyzer-checker-option-help -analyzer-checker-option-help-alpha`,
     stdout passed through, stderr discarded; on non-zero exit warn
     `'--dump-config clangsa' is not supported yet. Please make sure that you are using Clang 9 or newer.`
   - `cppcheck`: warn `'--dump-config cppcheck' is not supported.`
   - `gcc`, `infer`: unhandled "not implemented" error (stack trace, exit 1).
   - Return (exit 0).
3. `--analyzer-config <analyzer>`: header `Option`, `Description`
   (`option`, `description` for csv/json). Rows are
   `<analyzer>:<option>` and its documentation, from the analyzer's
   config list (see [analyzer-configuration.md](analyzer-configuration.md)).
   If the list is empty, warn
   `No analyzer configurations found for '<a>'. If you suspsect ...`.
   After printing, if this analyzer failed the availability check, warn
   `Can't analyze with '<a>': <reason>`.
4. Default listing: header `Name`, `Path`, `Version` (`name`, `path`,
   `version` for csv/json). One row per analyzer in registry order:
   name, resolved binary (empty if none), version string or `NOT FOUND`. Then
   one warning per failed analyzer: `Can't analyze with '<a>': <reason>`.

`-o` choices: `rows` (default), `table`, `csv`, `json`
([common-infrastructure.md](common-infrastructure.md) §7). `--all` and
`--details` are accepted and ignored.

Exit code 0, except the errors mentioned above.
