# Reference: analyzer and checker configuration

Usage: [../usage/analyzer-configuration.md](../usage/analyzer-configuration.md)

Python source: `analyzer/codechecker_analyzer/arg.py`, `cli/analyze.py`
(`is_analyzer_config_valid`, `is_checker_config_valid`,
`__transform_deprecated_flags`), `analyzers/*/analyzer.py`
(`construct_config_handler`), `codechecker_common/util.py`
(`load_args_from_file`).

## 1. Syntax

| Option | Regex (greedy) | Parsed into |
|---|---|---|
| `--analyzer-config` item | `(?P<analyzer>.+):(?P<option>.+)=(?P<value>.+)` | (analyzer, option, value) |
| `--checker-config` item | `(.+):(.+):(.+)=(.+)` | (analyzer, checker, option, value) |

Because the groups are greedy, the value starts after the **last** `=` and
(for `--analyzer-config`) the analyzer name ends at the colon that leaves a
valid `option=value` tail. Values cannot contain `=` (`HeaderFilterRegex=a=b`
parses as option `HeaderFilterRegex=a`, value `b`).

Both options take any number of items (`nargs='*'`) and can be repeated. Items
are kept in order; adding an item equal to an existing one moves it to the
end.

Deprecated: `--saargs F`, `--tidyargs F`, `--cppcheckargs F` become
`clangsa|clang-tidy|cppcheck:cc-verbatim-args-file=F` (appended), with a
warning `"--saargs" is deprecated. Use "--analyzer-config clangsa:cc-verbatim-args-file=<filepath>" instead.`
(likewise for the others).

## 2. Verbatim argument files

`cc-verbatim-args-file=F` (any analyzer): read `F`, strip it, replace every
`$(NAME)` with the environment variable `NAME` (unset → error
`NAME environment variable not set in F` and empty string), then shell-split
into arguments. These arguments are inserted into the analyzer command (see
[analyzer-drivers.md](analyzer-drivers.md) §5 for the position per analyzer).
A missing file → `File not found: F`, exit 1. The file is also copied into
`<out>/conf/`.

## 3. Validation (in `analyze`/`check`, before analysis)

### `--analyzer-config`

1. "Available analyzers" = analyzers whose binary version can be read.
   If `--analyzers` is given, restrict to those (intersection), if non-empty.
2. Get each available analyzer's option list
   ([analyzer-drivers.md](analyzer-drivers.md) §4). A failure → message
   `Could not get config for analyzer '<a>'. Error: <e>`.
3. For each item:
   - `--analyzers` given and item's analyzer not among them →
     `The analyzer '<a>' is not enabled. Enable it first using --analyzers to set the config '<option>'.`
   - analyzer not available →
     `Invalid argument to --analyzer-config: '<a>' is not an available analyzer. Available analyzers are: <list>.`
   - option not in the list →
     `Invalid argument to --analyzer-config: <a> has no config named '<option>'. Use the 'CodeChecker analyzers --analyzer-config <a>' command to see available configs.`
   - value check: options typed "existing path" (`cc-verbatim-args-file`)
     must exist → `Invalid value to --analyzer-config: '<a>:<o>=<v>'. File does not exist: <abs path>`.
     All other options are strings (always valid).
4. Every message is logged as an error; result is "invalid" if any.

### `--checker-config`

For each item: the analyzer must be one of the five
(`Invalid argument to --checker-config: '<a>' is not a supported analyzer. Supported analyzers are: ...`),
and `(checker, option)` must be in that analyzer's checker option list
(`Invalid argument to --checker-config: invalid checker '<c>' and/or checker option '<o>' for <a>. Use the 'CodeChecker checkers --checker-config' command to see available checker options.`).
Option lists are fetched for all five analyzers, even unused ones.

Invalid config + no `--no-missing-checker-error` → hint and exit 1 (see
[analyze.md](analyze.md) §2 step 3). With the flag, the invalid items are
still passed on to the analyzers.

## 4. How options reach each analyzer

| Analyzer | `--analyzer-config A:K=V` | `--checker-config A:C:O=V` |
|---|---|---|
| clangsa | `-Xclang -analyzer-config -Xclang K=V` | `-Xclang -analyzer-config -Xclang C:O=V` (emitted before the analyzer options) |
| clang-tidy | key in the `-config` JSON (`{"K": "V"}`); special keys `take-config-from-directory`, `Checks` | `CheckOptions: [{"key": "C.O", "value": "V"}]` in the `-config` JSON |
| cppcheck | `platform` (first value), `inconclusive`, `addons`, `libraries` (all values; repeatable) | ignored |
| gcc | only `cc-verbatim-args-file` | ignored |
| infer | only `cc-verbatim-args-file` | ignored |

Details of each command line, including the clang-tidy JSON merge rules
(default `HeaderFilterRegex=.*`, `-config` from verbatim args wins,
`take-config-from-directory`), are in
[analyzer-drivers.md](analyzer-drivers.md) §5.

## 5. `.clang-tidy` files

With `--analyzer-config clang-tidy:take-config-from-directory=true`:
- `-checks` is not passed (clang-tidy ≥ 19 gets `--allow-no-checks`);
- `-config` is not passed;
- `clang-diagnostic-*` checkers are not added to the state table, so no
  `-W`/`-Wno-` flags come from checker states;
- clang-tidy then reads the nearest `.clang-tidy` of each source file.

## 6. Z3 refutation

`--z3-refutation on` adds
`-Xclang -analyzer-config -Xclang crosscheck-with-z3=true` to clangsa. The
default is `on` only if `<clang> -cc1 -analyzer-config-help` contains
`crosscheck-with-z3` **and** a dummy analysis with
`-Xclang -analyzer-constraints=unsupported-z3` (newer Clang) or
`-Xclang -analyzer-constraints=z3` succeeds. Asking for `on` when the second
test fails is an error (see [analyze.md](analyze.md) §2 step 5).
