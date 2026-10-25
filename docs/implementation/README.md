# Implementation decisions

Architecture and design decisions for the C++ port. Each page says whether it
is **proposed** (under discussion) or **accepted**.

## Project-wide decisions

These are **accepted** and apply to every page below.

- **C++ version: C++20.** No C++23 features (`std::print`, `std::expected`,
  `std::ranges::to`, `std::stacktrace`, ...), even where the compiler has
  them.
- **Build system: CMake.**
- **Platform: Linux only.** No macOS or other systems.
- **No Python code in this repository.** Nothing is written in Python, and
  nothing copied from the Python repository may contain Python code.
- **The Python version never starts C++ components.** The Python launcher
  and Python commands run only Python code, and C++ programs run only C++
  code. The two versions meet only through files on disk and the Thrift API.
- **Testing: the black-box suite first.** CodeChecker's own tests (Python
  unit and functional tests, including ldlogger's) are not migrated to this
  repository, at least as a first step. The port is checked mainly by the
  black-box suite in [testing/](../../testing/README.md), which is extended
  as needed: new use cases, new test projects, or new options such as running
  the client and the server from different builds.
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
| [libraries.md](libraries.md) | accepted | What the C++ standard library covers, what we write ourselves, and which third-party libraries we use |
| [architecture.md](architecture.md) | proposed | How the port is structured: source tree, libraries, programs, processes, and how each part works |
