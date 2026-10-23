# CodeChecker++

CodeChecker++ is a port of [CodeChecker](https://github.com/Ericsson/codechecker)
from Python to C++.

The original Python project lives in `../codechecker` (version 6.30.0) and is
the reference for how the port should behave.

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

See `../codechecker/docs/architecture.md` for an overview of how these parts
work together.

## Layout

Not decided yet. It will be documented here once the first component is
ported.
