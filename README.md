# CodeChecker-cpp

CodeChecker-cpp is a port of [CodeChecker](https://github.com/Ericsson/codechecker)
from Python to C++.

The original Python project is
[Ericsson/codechecker](https://github.com/Ericsson/codechecker). The
documentation here is based on package version 6.30.0, commit
[`451b0c5c`](https://github.com/Ericsson/codechecker/tree/451b0c5c1786ed21b0685c298f759164a8c5d379), which is the reference for how the port should behave.

## Status

The port has not started yet. This directory contains documentation only.
Nothing has been implemented.

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

## Layout

Not decided yet. It will be documented here once the first component is
ported.
