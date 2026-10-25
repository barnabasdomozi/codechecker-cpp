# Black-box test suite

This test suite checks that a CodeChecker build behaves as described in
[docs/usage/](../docs/usage/README.md) and
[docs/reference/](../docs/reference/README.md). It runs a built CodeChecker
from the outside: it starts the binaries with command line arguments and checks
what they print, which files they write and which exit code they return. It
does not look at the source code of the implementation it tests.

## Status

Implemented. Every use case marked ✅, ⚠️ or ❌ in `docs/usage/` has at
least one test. The whole suite passes on the Python reference (CodeChecker
6.30.0); tests that need a missing optional tool are skipped. Where the
tests showed that the docs were wrong, the docs were corrected; bugs of the
reference are in [known-issues.md](../docs/usage/known-issues.md).

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
- **A set of C/C++ test projects.** The suite uses several small projects
  with known bugs and known expected results, each one covering different
  features (see [Test projects](#test-projects)).
- **The docs stay the source of truth.** The tests are organized the same way
  as the docs, so it is easy to see which use case a test belongs to and
  which use cases have no tests yet.

## How to run it

```sh
cd blackbox_tests
npm install                      # once

# 1. Set up the environment of the build under test, if it needs one.
#    The Python reference only runs inside its virtual environment:
source <codechecker-repo>/venv_dev/bin/activate

# 2. Optional tools some tests need (see "Optional tools" below), e.g. on
#    Debian, where clang-apply-replacements and clang-extdef-mapping are
#    not on PATH:
export PATH=/usr/lib/llvm-19/bin:$PATH

# 3. Run the suite on a build.
CODECHECKER_BIN=/abs/path/to/CodeChecker/bin/CodeChecker npm test
```

The interface is a set of environment variables:

| Variable | Meaning |
|---|---|
| `CODECHECKER_BIN` | **Required.** Absolute path of the `CodeChecker` executable to test. The standalone tools (`report-converter`, `tu_collector`, `merge-clang-extdef-mappings`, `post-process-stats`, `memory_safety_reporter`, `bazel-compile-commands`, `gerrit_changed_files_to_skipfile.py`, `ldlogger`) are looked up in the same folder, and `ld_logger/lib/64bit/ldlogger.so` next to that folder. |
| `CODECHECKER_COMPARE_BIN` | Optional. A second build for the [comparison tests](#comparing-output). |
| `CODECHECKER_TEST_KEEP` | `1` keeps the session folder (printed at the start) for debugging. |

The suite is **not** told which implementation the build is, and it never
behaves differently depending on that. **Setting up the environment is not
the suite's job**: everything a build needs to run is done in the shell
before the suite starts. The suite passes its own environment on to the
binaries it runs.

Useful variations:

```sh
npx jest tests/parse.test.ts                 # one page
npx jest tests/server                        # the server use cases
npx jest -t 'Print the bug path'             # tests whose name matches
npx jest -t '^(?!.*\[fixed: known-issues)'   # leave out tests for bugs the port fixes
npm run typecheck                            # type check with tsc (TypeScript 7)
```

A full run on the Python reference takes several minutes; most of the time is
spent in the analyzers and in starting servers.

### Optional tools

A test whose tool is missing is skipped, and its name ends with
`(skipped: needs <tool>)`:

| Tool | Used by |
|---|---|
| `clang-apply-replacements` | `fixit` |
| `clang-extdef-mapping` | `merge-clang-extdef-mappings` |
| `script` (util-linux) | interactive `fixit -i` |
| `pylint` | `report-converter -t pylint` |
| `openssl` | HTTPS server tests |
| `unzip` | `tu_collector` tests that look inside the zip |
| PostgreSQL server binaries (`initdb`, `pg_ctl`, `psql`) | the PostgreSQL use cases (see below) |
| `infer`, `bazel` | the remaining ❌ use cases of the docs |

Also needed, as for any CodeChecker use: `make`, `cmake`, `gcc`/`g++`,
`clang`/`clang++` (with sanitizer support), `clang-tidy` and **Cppcheck**
(see below; the suite stops at the start if Cppcheck is not on `PATH`).

### Cppcheck and the documented results

The docs and the project READMEs show the results of Clang SA, Clang-Tidy,
GCC and Cppcheck 2.17.1 (no Infer), the analyzers of the test machine. A
plain `analyze` runs every analyzer it finds, so on a machine with other
analyzers it gives other results. Tests that compare results with the docs
therefore pass `--analyzers clangsa clang-tidy gcc cppcheck`
(`DOC_ANALYZERS` in `src/projects.ts`); the shared analyses do the same.
Tests that check the default behavior itself ("every analyzer that is found
runs") do not. Another Cppcheck version may report differently.

### PostgreSQL

The PostgreSQL tests (`tests/server/postgresql.test.ts`) never use a
PostgreSQL server that is already running on the machine. They create a
temporary cluster with `initdb` in the test's sandbox folder, start it with
`pg_ctl` on a free port (password login on localhost, and a Unix socket in
the sandbox for setting up roles and databases), and stop and delete it at
the end. The binaries are taken from `PATH`, or else from the newest
`/usr/lib/postgresql/<version>/bin` (Debian/Ubuntu). The CodeChecker build
needs its own PostgreSQL support (the Python reference uses `psycopg2`).

## What is checked

- **Exit codes**, as listed in
  [exit-codes.md](../docs/reference/exit-codes.md). This includes the
  different rules for the `CodeChecker` launcher (argument errors exit 1) and
  the standalone tools (argparse errors exit 2), and `143` after a server is
  stopped with `SIGTERM`.
- **stdout and stderr**: report lines, summary tables, warnings and error
  messages.
- **Files written**: report folders, plist, JSON, SARIF, Code Climate,
  Gerrit, baseline and HTML output, `compile_cmd.json`, skip files, zip
  files, and server workspaces.
- **Server use cases**: `server`, `store` and every `cmd` subcommand, against
  real servers started by the suite. Authentication, HTTPS and products are
  included.

## Layout

```
blackbox_tests/
├── package.json, jest.config.js, babel.config.js, tsconfig.json
├── src/                  helpers
│   ├── config.ts         the environment variables
│   ├── run.ts            running commands (cc(), tool(), run())
│   ├── sandbox.ts        a folder, HOME and TMPDIR per test file
│   ├── shared.ts         results made once per session (shared analyses)
│   ├── projects.ts       the test projects: copy, log, analyze
│   ├── reports.ts        parse -e json, plain report lines, Markdown tables
│   ├── server.ts         starting servers, store/results/runs helpers
│   ├── postgres.ts       a temporary PostgreSQL server
│   ├── chain.ts          command chains
│   ├── usecase.ts        useCase(): a test for a documented use case
│   ├── normalize.ts      removing paths, timestamps and durations
│   ├── requirements.ts   optional tools
│   ├── zip.ts            reading zip files with `unzip`
│   └── matchers.ts       toExitWith(), toOutput()
└── tests/
    ├── <page>.test.ts    one file per docs/usage page (some pages share one)
    ├── server/           pages that need a running server
    ├── chains/           command chains
    ├── analyze-targets/  the expected results of each test project
    ├── compare.test.ts   the comparison mode
    └── doc-coverage.test.ts
```

## Test organization

- A use-case test is declared with
  `useCase('<page>.md', '<heading>', fn, options)`, inside
  `describe('usage/<page>.md')`. The heading is the docs heading without
  its ✅/⚠️/❌ mark and without backticks.
- `tests/doc-coverage.test.ts` checks that every `useCase()` names an
  existing heading, and lists every documented use case without a test as a
  Jest "todo" (none at the moment).
- Test names carry tags:
  - `[known-issues #N]`: the test covers entry N of
    [known-issues.md](../docs/usage/known-issues.md) and expects the
    reference behavior.
  - `[fixed: known-issues #N]`: reserved for tests that expect the port's
    fixed behavior (none yet). They are expected to fail on the reference.
- Expected results are read from the docs where they are written down as
  tables: the project READMEs in
  [blackbox_tests/analyze-targets/](analyze-targets/README.md)
  and [reference/demo-project.md](../docs/reference/demo-project.md).

## Command chains

Most real use of CodeChecker is a chain of commands, where one command reads
the files or server data that the previous one wrote. Testing each command
alone, with prepared input, does not show that these steps work together.
So the suite also runs chains (`tests/chains/`), from short ones:

- `log` → `analyze` → `parse` (plain, `-e json`, `-e sarif`, `-e html`,
  `-e baseline` followed by `cmd diff`);
- two versions analyzed → `cmd diff --new/--resolved/--unresolved`;
- sanitizer programs → `report-converter` → `parse`;

to a long one that follows a full workflow with a server: `log` →
`analyze` → `parse` → `store` → `cmd runs/results/sum` → `cmd suppress` →
`analyze` the next version → `cmd diff` → `store` again → `cmd diff` of the
two tags → `cmd history` → `cmd export` → `cmd import` into a new product.

In a chain:

- Each step uses only what earlier steps produced, not prepared files.
- Each step is its own test, and its output is checked, so a failure points
  to the step that caused it.
- When a step fails, the remaining steps are not run: they fail at once with
  `NOT RUN: the earlier step '<step>' failed.` (Jest cannot skip a test after
  the run has started.)

## Test projects

The projects are in
[blackbox_tests/analyze-targets/](analyze-targets/README.md). The
[demo project](../docs/reference/demo-project.md) from the docs is one of
them. The others cover features the demo project does not:

- different build systems (Make, recursive Make, CMake, a shell script),
  for `log` and for compilation databases written by other tools;
- C and C++ sources, headers, and code that only some analyzers report on;
- cross translation unit (CTU) bugs, where the bug spans more than one
  source file;
- source-code suppression comments and review statuses;
- sources that fail to compile, for failed-analysis exit codes;
- a project with no bugs, for the "no reports" paths;
- paths with spaces or non-ASCII characters;
- two versions of the same project, for `cmd diff` and local diffs;
- output of other tools (sanitizers, Clang-Tidy, GCC) for
  `report-converter`.

`tests/analyze-targets/` checks each project against the tables in its README.
Statistical analysis has no project yet. See
[Not covered yet](analyze-targets/README.md#not-covered-yet).

## Comparing output

- **Normalization.** Before comparing, the suite replaces values that change
  from run to run: project and temporary folder paths, timestamps and
  durations.
- **Structured formats are compared as data.** JSON and SARIF files are
  parsed and compared as values, so key order and whitespace do not cause
  failures. Text output, such as the tables printed by `parse`, is compared
  as text. Listings whose order is not fixed in the reference (label lists,
  option lists) are compared as sets.
- **Two ways to get the expected output:**
  - **Expected output from the docs.** The expected output is taken from
    the docs, either copied into the tests or read from the docs' tables.
  - **Comparing two builds** (`tests/compare.test.ts`, only with
    `CODECHECKER_COMPARE_BIN`). Both builds analyze every test project (with
    and without CTU); their exit codes, reports and `parse` output, and the
    output of `analyzers` and `checkers`, must be the same. This finds
    differences the docs do not cover. The environment must already be set
    up for both builds.

## Differences between the implementations

The docs mark each use case:

| Mark | Meaning for the suite |
|------|-----------------------|
| ✅ | The test expects the documented behavior from both implementations. |
| ⚠️ | The Python version has a bug or differs from its help text (see [known-issues.md](../docs/usage/known-issues.md)). By default the test expects the Python behavior, and the port copies it. When the port fixes the issue instead, the test expects the fixed behavior and is tagged `[fixed: known-issues #N]`. That decision is also written down next to the entry. These tagged tests are expected to fail on the Python reference. |
| ❌ | The use case could not be run on the test machine. The test is skipped until the needed tool is available, and it must be checked against the Python version before its result can be trusted. Left: Infer, Bazel, statistical analysis and the Clang SA plugin folder. |

## Shared setup

Some setup is slow, so the suite does it once and reuses it:

- Each test file gets a sandbox folder with its own `HOME` and `TMPDIR`, so
  sessions, password files and the list of running servers never leak
  between test files (and never touch the real home folder).
- The test projects are copied before use, so tests never change the
  originals.
- One `analyze` run of a project (with given options) is shared by all test
  files of a session (`sharedAnalysis()`); it is made by the first test that
  needs it. Tests treat it as read-only. `store` writes temporary files into
  report folders, so stores use a private copy of the folder.
- Command chains do not use shared results. They run every step themselves.
- Server tests start their own servers, each with its own workspace and a
  free port, and stop them at the end (also when a test fails), before the
  sandbox folder is removed.

Tests do not depend on the order of test files, and test files run in
parallel. Tests inside one file run in order and may build on each other.

## Tools

- **Language:** TypeScript (checked with `tsc --noEmit`, TypeScript 7).
- **Test framework:** [Jest](https://jestjs.io/) 30.
- **TypeScript in Jest:** Babel's TypeScript preset removes the types
  (`babel.config.js`). `ts-jest` does not support TypeScript 7, and
  `@swc/jest` needs a native binary that cannot be loaded on every machine.

## Out of scope

- Unit tests of the C++ code. These belong next to the C++ sources.
- Performance and load testing.
- The Web UI in a browser. The suite only checks that the server serves it;
  the API it uses is tested through the command line tools.
