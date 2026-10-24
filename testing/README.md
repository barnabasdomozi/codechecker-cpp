# Black-box test suite

This test suite checks that a CodeChecker build behaves as described in
[docs/usage/](../docs/usage/README.md) and
[docs/reference/](../docs/reference/README.md). It runs a built CodeChecker
from the outside: it starts the binaries with command line arguments and checks
what they print, which files they write and which exit code they return. It
does not look at the source code of the implementation it tests.

## Status

Planned. Nothing has been implemented yet. This page describes the goals.

## Goals

- **One suite, two implementations.** The same tests run against:
  - the Python CodeChecker 6.30.0 (git `451b0c5c`), which is the reference
    the docs were written from, and
  - the C++ port in this repository.

  The suite does not know which of the two it is testing. It only sees the
  binaries it is given. When the suite passes on the Python version (apart
  from the tests for bugs the port fixes, see
  [Differences between the implementations](#differences-between-the-implementations)),
  the tests are correct. When it passes on the C++ version, the port matches the
  reference.
- **Every documented use case is covered.** Each use case in `docs/usage/`
  and each rule in `docs/reference/` (options, error messages, exit codes,
  output formats) has at least one test.
- **Commands are tested alone and chained together.** Besides testing each
  command on its own, the suite runs commands one after another, the way
  users do, with each command working on what the previous one produced
  (see [Command chains](#command-chains)).
- **A set of C/C++ test projects.** The suite defines several small projects
  with known bugs and known expected results, each one covering different
  features (see [Test projects](#test-projects)).
- **The docs stay the source of truth.** The tests are organized the same way
  as the docs, so it is easy to see which use case a test belongs to and
  which use cases have no tests yet.

## How it is run

The suite takes only the **absolute path** to the build under test. It is
**not** told which implementation the build is (Python or C++), and it never
behaves differently depending on that. The exact interface (command line
option or environment variable) is not decided yet.

**Setting up the environment is not the suite's job.** Anything an
implementation needs in order to run is done **before** the suite is
started, in the shell that starts it. For the Python reference this means
activating its development virtual environment first:

```sh
source <codechecker-repo>/venv_dev/bin/activate
# then start the black-box suite with the path to the Python build
```

The suite passes its own environment on to the binaries it runs, so they
see the activated virtual environment. The C++ build needs no setup.

Things the runner has to handle:

- **More than one binary.** Besides `CodeChecker` there are standalone tools
  (`report-converter`, `tu_collector`, `merge-clang-extdef-mappings`,
  `post-process-stats`, `memory_safety_reporter`, `bazel-compile-commands`,
  ...). The runner needs the path of each one, or a bin directory that
  holds them.
- **Optional tools.** Some use cases need tools that may not be installed
  (Cppcheck, Facebook Infer, PostgreSQL, Bazel, `unzip`). Tests that need a
  missing tool are skipped, and the skip message says why.

## What is checked

- **Exit codes**, as listed in
  [exit-codes.md](../docs/reference/exit-codes.md). This includes the
  different rules for the `CodeChecker` launcher (argument errors exit 1) and
  the standalone tools (argparse errors exit 2), and `128 + signal` after a
  signal stops the process.
- **stdout and stderr**: report lines, summary tables, warnings and error
  messages. The suite also checks which stream each message goes to (for
  example, `parse -e json` logs to stderr).
- **Files written**: report folders, plist, JSON, SARIF, Code Climate,
  Gerrit, baseline and HTML output, `compile_cmd.json`, skip files, and
  server databases.
- **Server use cases**: `server`, `store` and every `cmd` subcommand, against
  a real server started by the suite. Authentication and product setup are
  included.

## Command chains

Most real use of CodeChecker is a chain of commands, where one command reads
the files or server data that the previous one wrote. Testing each command
alone, with prepared input, does not show that these steps work together.
So the suite also runs chains, from short ones:

- `analyze`, then `parse` the report folder;
- `analyze`, then `parse -e json` / `-e sarif` / `-e html`;
- `store`, then `cmd results` / `cmd sum` / `cmd runs`;
- `store` twice, then `cmd diff` between the two runs;

to long ones that follow a full workflow, for example:

- `log` the build, `analyze` the compilation database, `parse` the
  results, `store` them on a server, then `cmd results`, `cmd suppress`,
  `cmd diff`, `cmd export` and `cmd import`.

In a chain:

- Each step uses only what earlier steps produced, not prepared files.
- The output of each step is checked, not only the result at the end, so a
  failure points to the step that caused it.
- A step that fails stops the chain, and the remaining steps are reported as
  not run.

## Test projects

The suite defines several small C and C++ projects to run CodeChecker on.
Each project has known bugs, and the expected results (reports, checker
names, report counts, exit codes) are written down with the project.

The [demo project](../docs/reference/demo-project.md) from the docs is one
of them. Other projects are needed to cover features the demo project does
not, for example:

- different build systems (plain compiler commands, Make, CMake), for
  `log` and for compilation databases written by other tools;
- C and C++ sources, headers, and code that only some analyzers report on;
- cross translation unit (CTU) bugs, where the bug spans more than one
  source file;
- source-code suppression comments and review statuses;
- statistical analysis;
- sources that fail to compile, for failed-analysis exit codes;
- a project with no bugs, for the "no reports" paths;
- paths with spaces or non-ASCII characters;
- two versions of the same project, for `cmd diff` and local diffs.

The full list of projects and where they are kept is not decided yet.

## Comparing output

- **Normalization.** Before comparing, the suite replaces values that change
  from run to run: home and temporary directory paths (the docs show them as
  `~/demo`), timestamps, durations, port numbers and version strings.
- **Structured formats are compared as data.** JSON, plist and SARIF files
  are parsed and compared as values, so key order and whitespace do not
  cause failures. Text output, such as the tables printed by `parse`, is
  compared as text.
- **Two ways to get the expected output:**
  - **Expected output from the docs.** The expected output is copied from
    the docs into the tests.
  - **Comparing two builds (optional mode).** The suite is given a second
    build path and runs both builds on the same input, then compares their
    results. This finds differences the docs do not cover. The suite still
    does not need to know which build is which, and the environment must
    already be set up for both builds before the suite starts.

## Differences between the implementations

The docs mark each use case:

| Mark | Meaning for the suite |
|------|-----------------------|
| ✅ | The test expects the documented behavior from both implementations. |
| ⚠️ | The Python version has a bug or differs from its help text (see [known-issues.md](../docs/usage/known-issues.md)). By default the test expects the Python behavior, and the port copies it. When the port fixes the issue instead, the test expects the fixed behavior and is tagged with its `known-issues.md` entry. That decision is also written down next to the entry. These tagged tests are expected to fail on the Python reference. |
| ❌ | The use case was not run when the docs were written. The test is skipped until the needed tool is available, and it must be checked against the Python version before its result can be trusted. |

## Test organization

- One test file per `docs/usage/*.md` page, named after the page.
- Each test names the doc section it covers (the page and heading).
- A small coverage check lists the ✅ and ⚠️ use cases in the docs that have
  no test yet.

## Shared setup

Some setup is slow, so the suite does it once and reuses it:

- The test projects are copied to a temporary directory, so tests never
  change the originals.
- One `analyze` run of each test project is reused by the single-command
  `parse`, `store` and `cmd` tests, which only read the results. Command
  chains do not use these shared results. They run every step themselves.
- A CodeChecker server, with its own workspace and a free port, is started
  for the server tests and stopped at the end. Tests that change server state
  use their own products or runs, so they don't affect each other.

Tests must not depend on the order they run in. They must also be able to
run in parallel, except where they share one server.

## Tools

- **Language:** TypeScript.
- **Test framework:** [Jest](https://jestjs.io/).

Jest features the suite is expected to use:

- global setup and teardown for the demo project and the server;
- `test.each` to run one case with several inputs;
- conditional `test.skip` for missing tools;
- test name filters, to leave out or select the tests tagged with a
  `known-issues.md` entry;
- snapshot files for long outputs;
- parallel workers.

How TypeScript is compiled for Jest (for example `ts-jest`) is not decided
yet.

## Out of scope

- Unit tests of the C++ code. These belong next to the C++ sources.
- Performance and load testing.
- The Web UI in a browser. Only the files and API that the Web UI uses are
  tested, through the command line tools.
