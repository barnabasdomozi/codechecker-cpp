# Implementation decisions

Architecture and design decisions for the C++ port. Each page says whether it
is **proposed** (under discussion) or **accepted**.

## Project-wide decisions

These are **accepted** and apply to every page below.

- **Coding guideline: [cppguide.md](cppguide.md).** All C++ code must follow
  it at all times. It is the only coding guideline of the project: the
  language version (C++20), naming, formatting, which language features may
  be used, and the project's waivers (exceptions only at the Thrift boundary,
  `std::filesystem` allowed, namespace `cc`) are all defined there. So are
  the project principles: avoid overengineering, prefer minimal solutions,
  write reusable functions, use the third-party libraries fully, and test
  every ported module on the black-box suite. No other document adds or
  changes coding rules.
- **Build system: CMake. Every third-party library is a git submodule,
  compiled from source** and linked statically (see
  [build-system.md](build-system.md) for the exceptions).
- **Platform: Linux only.** No macOS or other systems.
- **No Python code in this repository.** Nothing is written in Python, and
  nothing copied from the Python repository may contain Python code.
- **The Python version never starts C++ components.** The Python launcher
  and Python commands run only Python code, and C++ programs run only C++
  code. The two versions meet only through files on disk and the Thrift API.
- **Testing: the black-box suite first.** CodeChecker's own tests (Python
  unit and functional tests, including ldlogger's) are not migrated to this
  repository, at least as a first step. The port is checked mainly by the
  black-box suite in [blackbox_tests/](../../blackbox_tests/README.md),
  which is extended as needed: new use cases, new test projects, or new
  options such as running the client and the server from different builds.
- **The web UI is copied, not changed.** The Vue sources
  (`web/server/vue-cli` in the Python repository, built into `www/`) are
  copied as they are. The C++ server serves the same files and the same
  Thrift API, so the web UI works without changes.
- **ldlogger is copied, not changed.** The C sources of the build logger
  (`analyzer/tools/build-logger/src/`, and its `README.md`) are copied as they
  are, and CMake compiles them. Its `Makefile`, Python unit tests
  (`tests/`, `pytest.ini`, `requirements_py/`) are not copied: the Makefile
  includes the Python test targets and sets up a Python environment, and
  CodeChecker's tests are not migrated (see "Testing" above). The black-box
  suite tests ldlogger through `CodeChecker log`.
- **The Thrift IDL files are copied, not changed.** The `.thrift` files from
  `codechecker_api/` are copied as they are, and the C++ code is generated
  from them (see [libraries.md](libraries.md#rpc-apache-thrift)).

## Pages

| Page | Status | Topic |
|---|---|---|
| [cppguide.md](cppguide.md) | accepted | The C++ coding guideline (Google C++ Style Guide with the project's waivers); mandatory for all C++ code |
| [libraries.md](libraries.md) | accepted | What the C++ standard library covers, what we write ourselves, and which third-party libraries we use |
| [build-system.md](build-system.md) | accepted | Third-party libraries as git submodules, the deps build, CMake options and presets, ldlogger, web UI, the build folder as the package, CI |
| [architecture.md](architecture.md) | accepted | How the port is structured: source tree, libraries, programs, processes, and how each part works |
