# CodeChecker-cpp

CodeChecker-cpp is a port of [CodeChecker](https://github.com/Ericsson/codechecker)
from Python to C++.

The original Python project is
[Ericsson/codechecker](https://github.com/Ericsson/codechecker). The
documentation here is based on package version 6.30.0, commit
[`451b0c5c`](https://github.com/Ericsson/codechecker/tree/451b0c5c1786ed21b0685c298f759164a8c5d379), which is the reference for how the port should behave.

## Status

The main program is `codechecker-cpp` (the Python version's `CodeChecker`).
The porting order is in
[architecture.md](docs/implementation/architecture.md#14-porting-order).

| Mark | Meaning |
|------|---------|
| ✅ | Ported |
| 🟡 | Partly ported (the note says what is missing) |
| ❌ | Not started |

| Area | Component | Usage page | Status | Notes |
|------|-----------|------------|--------|-------|
| Foundation | Build system, third-party libraries | [build-system.md](docs/implementation/build-system.md) | ✅ | CMake presets, deps build, unit tests |
| Foundation | `cc_platform`, `cc_util` | | ✅ | Processes, files, argument parsing, logging, tables, JSON, YAML, plist, Python-compatible helpers |
| Foundation | `cc_report` | [report-format](docs/reference/report-format.md), [report-hash](docs/reference/report-hash.md) | ✅ | Report model, report hashes, plist and SARIF reading and writing |
| Foundation | `cc_common`, launcher | | ✅ | Checker labels, skip lists, review status handling, `--config` files, the launcher's second process |
| Information | `version`, `analyzer-version`, `web-version` | [version.md](docs/usage/version.md) | ✅ | |
| Information | `analyzers` | [analyzers.md](docs/usage/analyzers.md) | ❌ | |
| Information | `checkers` | [checkers.md](docs/usage/checkers.md) | ❌ | |
| Analysis | `log`, `ldlogger` | [log.md](docs/usage/log.md) | ✅ | |
| Analysis | `analyze` | [analyze.md](docs/usage/analyze.md) | ✅ | |
| Analysis | Analyzer drivers | [analyzers.md](docs/usage/analyzers.md) | ✅ | Clang SA, Clang-Tidy, Cppcheck, GCC, Infer |
| Analysis | Checker selection | [checker-selection.md](docs/usage/checker-selection.md) | ✅ | |
| Analysis | Analyzer configuration | [analyzer-configuration.md](docs/usage/analyzer-configuration.md) | ✅ | |
| Analysis | Compiler info | [compiler-info.md](docs/usage/compiler-info.md) | ✅ | |
| Analysis | `--config` files | [config-file.md](docs/usage/config-file.md) | 🟡 | `analyze` only; `parse`, `check`, `store` and `server` come with those commands |
| Analysis | Skip and file filters | [skip-and-file-filters.md](docs/usage/skip-and-file-filters.md) | 🟡 | `analyze` only; `parse` filters come with `parse` |
| Analysis | CTU analysis | [ctu-analysis.md](docs/usage/ctu-analysis.md) | ✅ | |
| Analysis | Statistical analysis | [statistical-analysis.md](docs/usage/statistical-analysis.md) | 🟡 | `analyze --stats` is ported; the `post-process-stats` program is not |
| Analysis | Makefile mode | [makefile-mode.md](docs/usage/makefile-mode.md) | ✅ | |
| Analysis | Analysis debugging | [analysis-debugging.md](docs/usage/analysis-debugging.md) | ✅ | |
| Analysis | `check` | [check.md](docs/usage/check.md) | ❌ | |
| Local results | `parse` | [parse.md](docs/usage/parse.md) | ❌ | |
| Local results | `parse --export` | [parse-export.md](docs/usage/parse-export.md) | ❌ | HTML, JSON, Code Climate, SARIF, Gerrit, baseline |
| Local results | Review status and suppression | [review-status-and-suppression.md](docs/usage/review-status-and-suppression.md) | 🟡 | Review status handling is ported for `analyze` results; `parse`, `check` and suppress files are not |
| Local results | `fixit` | [fixit.md](docs/usage/fixit.md) | ❌ | |
| Local results | Local diff (`cmd diff` on folders) | [local-diff.md](docs/usage/local-diff.md) | ❌ | |
| Server | Thrift API (`cc_api`) | [server-api](docs/reference/server-api.md) | 🟡 | `api_version.json` only; no generated stubs yet |
| Server | `server` | [server.md](docs/usage/server.md) | ❌ | |
| Server | Server database | [server-database.md](docs/usage/server-database.md) | ❌ | |
| Server | Authentication | [server-authentication.md](docs/usage/server-authentication.md) | ❌ | |
| Server | `store` | [store.md](docs/usage/store.md) | ❌ | |
| Server | Web UI | [web-ui.md](docs/usage/web-ui.md) | ❌ | |
| `cmd` client | `cmd runs`, `cmd history` | [cmd-runs-history.md](docs/usage/cmd-runs-history.md) | ❌ | |
| `cmd` client | `cmd results` | [cmd-results.md](docs/usage/cmd-results.md) | ❌ | |
| `cmd` client | `cmd sum` | [cmd-sum.md](docs/usage/cmd-sum.md) | ❌ | |
| `cmd` client | `cmd diff` | [cmd-diff.md](docs/usage/cmd-diff.md) | ❌ | |
| `cmd` client | Run management | [cmd-run-management.md](docs/usage/cmd-run-management.md) | ❌ | |
| `cmd` client | `cmd suppress` | [cmd-suppress.md](docs/usage/cmd-suppress.md) | ❌ | |
| `cmd` client | Export and import | [cmd-export-import.md](docs/usage/cmd-export-import.md) | ❌ | |
| `cmd` client | `cmd products` | [cmd-products.md](docs/usage/cmd-products.md) | ❌ | |
| `cmd` client | `cmd components` | [cmd-components.md](docs/usage/cmd-components.md) | ❌ | |
| `cmd` client | `cmd filter-preset` | [cmd-filter-preset.md](docs/usage/cmd-filter-preset.md) | ❌ | |
| `cmd` client | `cmd serverside-tasks` | [cmd-serverside-tasks.md](docs/usage/cmd-serverside-tasks.md) | ❌ | |
| Tools | `report-converter` | [report-converter.md](docs/usage/report-converter.md) | ✅ | All 28 input types |
| Tools | `tu_collector` | [tu-collector.md](docs/usage/tu-collector.md) | 🟡 | The library is ported and used by `analyze`; the standalone program is not |
| Tools | `merge-clang-extdef-mappings` | [merge-clang-extdef-mappings.md](docs/usage/merge-clang-extdef-mappings.md) | 🟡 | The merge is ported inside `analyze` (CTU); the standalone program is not |
| Tools | Gerrit integration | [gerrit-integration.md](docs/usage/gerrit-integration.md) | ❌ | |
| Tools | `memory_safety_reporter` | [memory-safety-reporter.md](docs/usage/memory-safety-reporter.md) | ❌ | |
| Tools | `bazel-compile-commands` | [bazel-compile-commands.md](docs/usage/bazel-compile-commands.md) | ❌ | |
| Reference | Environment variables | [environment-variables.md](docs/usage/environment-variables.md) | 🟡 | Supported by the ported commands |
| Reference | Exit codes | [exit-codes.md](docs/usage/exit-codes.md) | 🟡 | Supported by the ported commands |

Some black-box tests need commands that are not ported yet. The
`report-converter` tests that read results back need `parse`; they pass with
the C++ `report-converter` next to the Python `CodeChecker`. The `analyze`
tests run with a wrapper that sends `analyze` to `codechecker-cpp` and the
other subcommands to the Python `CodeChecker`.

## Goals

- Match the behavior of the Python CodeChecker: same command line interface,
  same input and output formats (compilation databases, plist files, reports),
  so the C++ version has feature parity with the Python one and works exactly
  the same way.
- Port one component at a time, testing each against the Python version.

## Scope

The main parts of the Python project that need porting:

| Python component                          | What it does                                                        |
|-------------------------------------------|---------------------------------------------------------------------|
| `analyzer/` (`codechecker_analyzer`)      | Logs builds, prepares compile commands, runs analyzers (Clang Static Analyzer, Clang-Tidy, Cppcheck, GCC, ...), handles CTU and statistical analysis |
| `tools/report-converter`                  | Parses plist and other analyzer outputs and converts between formats |
| `tools/tu_collector`                      | Collects the source files of a translation unit                     |
| `codechecker_common`                      | Shared helpers: logging, configuration, CLI utilities                |
| `web/` (`codechecker_web`, server, client)| Report storage server, Thrift API, database, command line client     |
| `codechecker_api`                         | Thrift API definitions                                               |

See [docs/architecture.md](https://github.com/Ericsson/codechecker/blob/451b0c5c1786ed21b0685c298f759164a8c5d379/docs/architecture.md) in the CodeChecker
repository for an overview of how these parts
work together.

## Documentation

[docs/usage/](docs/usage/README.md) describes every use case of the Python
CodeChecker 6.30.0, one page each, tested against a real build. The C++
version must support the same commands, options, outputs and exit codes.
[docs/usage/known-issues.md](docs/usage/known-issues.md) lists bugs found in
the Python version while testing.

[docs/implementation/](docs/implementation/README.md) holds the decisions
for the C++ port: libraries, architecture and the coding guideline.

## Coding guideline

All C++ code in this repository **must** follow
[docs/implementation/cppguide.md](docs/implementation/cppguide.md) at all
times. It is the only coding guideline of the project; no other document adds
or changes coding rules.

## Building

Linux only. The host tools are listed in
[build-system.md](docs/implementation/build-system.md#1-host-tools)
(on Debian also `apt install flex bison gcc-multilib`).

```sh
third_party/checkout.sh                  # the submodules, about 1 GB
cmake --preset deps                      # the third-party libraries, once
cmake --build --preset deps --parallel
cmake --preset dev                       # the project
cmake --build --preset dev --parallel
ctest --preset dev
build/dev/bin/codechecker-cpp version    # runs from the build folder
```

Every preset builds in `build/<preset>`; no folder needs to be given. To run
the black-box tests of a page on an installed build:

```sh
cmake --build --preset dev --target install   # into build/dev/install
cd blackbox_tests && CODECHECKER_BIN=$PWD/../build/dev/install/bin/codechecker-cpp npx jest tests/version.test.ts
```

Other presets: `dev-clang`, `asan`, `release` and `analyzer` (the analyzer
side only). Never check out the submodules with `--recursive`; see
[build-system.md](docs/implementation/build-system.md#23-checkout).

## Layout

The source tree is described in
[architecture.md](docs/implementation/architecture.md#2-source-tree).
