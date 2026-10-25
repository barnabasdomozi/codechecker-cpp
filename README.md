# CodeChecker-cpp

CodeChecker-cpp is a port of [CodeChecker](https://github.com/Ericsson/codechecker)
from Python to C++.

The original Python project is
[Ericsson/codechecker](https://github.com/Ericsson/codechecker). The
documentation here is based on package version 6.30.0, commit
[`451b0c5c`](https://github.com/Ericsson/codechecker/tree/451b0c5c1786ed21b0685c298f759164a8c5d379), which is the reference for how the port should behave.

## Status

The build system is set up (see [Building](#building)): the third-party
libraries, ldlogger and a test of the libraries build. No component has
been ported yet.

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
cmake -S third_party -B build/deps       # the third-party libraries, once
cmake --build build/deps --parallel
cmake --preset dev                       # the project
cmake --build --preset dev --parallel
ctest --preset dev
```

Other presets: `dev-clang`, `asan`, `release` and `analyzer` (the analyzer
side only). Never check out the submodules with `--recursive`; see
[build-system.md](docs/implementation/build-system.md#23-checkout).

## Layout

The source tree is described in
[architecture.md](docs/implementation/architecture.md#2-source-tree).
