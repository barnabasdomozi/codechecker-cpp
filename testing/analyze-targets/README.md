# Test projects

Small C/C++ projects for the [black-box test suite](../README.md).
Each project has known bugs and known expected results, and each one covers
different CodeChecker features.

| Project | What it is for |
|---|---|
| [demo](demo/README.md) | The demo project of the [docs](../../docs/usage/demo-project.md), and its second version. The expected outputs in `docs/usage/` come from it. |
| [cmake-app](cmake-app/README.md) | CMake build with a library, an app and third-party code. Bugs in headers, skipfiles, the CMake compilation database. |
| [make-recursive](make-recursive/README.md) | Recursive Make. Macros, a force-included header and a response file that must reach the analyzer. One file compiled twice (compile uniqueing). |
| [ctu](ctu/README.md) | Bugs that are only found with cross translation unit analysis, including one across three files. |
| [review-status](review-status/README.md) | Every kind of source-code review status comment, a `review_status.yaml` rules file and a suppress file. |
| [build-failures](build-failures/README.md) | A failing build and sources that cannot be analyzed: failed-analysis exit codes and `failed/` output. |
| [clean](clean/README.md) | A project with no reports, for the "nothing found" paths. |
| [odd-paths](odd-paths/README.md) | Paths with spaces, non-ASCII characters and an apostrophe, built by a shell script. |
| [analyzer-features](analyzer-features/README.md) | Reports from Clang SA, Clang-Tidy and GCC, checker profiles, fix-its, long bug paths and macros. |
| [external-tools](external-tools/README.md) | Output of sanitizers, Clang-Tidy and the GCC analyzer run directly, for `report-converter`. |

## Rules for using a project

- **Copy the project to a temporary directory first**, and build and
  analyze the copy. Never build in this directory: the sources must stay
  unchanged, and build outputs must not be committed.
- Report locations in the project pages are relative to the project's root
  directory.
- Report hashes depend only on file base names, line contents, columns,
  checker names and messages
  ([report-hash.md](../../docs/reference/report-hash.md)), so they are the
  same in any directory.

## How the expected results were produced

All results on the project pages were produced on 2026-10-24 with the same
setup as the docs ([usage/README.md](../../docs/usage/README.md)):

- CodeChecker 6.30.0 (git `451b0c5c`), Python 3.13, run from its
  `venv_dev` virtual environment;
- Clang and Clang-Tidy 19.1.7, GCC 14.2.0, CMake 3.31.6, GNU Make;
- not installed: Cppcheck, Facebook Infer, Bear, Ninja.

Unless a page says otherwise, the commands were:

```sh
CodeChecker log -b "<build command>" -o compile_commands.json
CodeChecker analyze compile_commands.json -o reports
CodeChecker parse reports
CodeChecker parse reports -e json \
  --review-status confirmed false_positive intentional unreviewed
```

The tables on the project pages list every report from the last command.
Plain `parse` hides `false_positive` and `intentional` reports.

## Not covered yet

- **Statistical analysis** (`--stats`). It needs a Clang with the
  statistics collector checkers, which Debian's Clang 19 does not have (see
  [statistical-analysis.md](../../docs/usage/statistical-analysis.md)).
- **Cppcheck and Infer** results. The projects can be analyzed with them
  once they are installed, but there are no expected results yet.
- **Bazel** (`bazel-compile-commands`): Bazel is not installed.

Server use cases (`store`, `cmd ...`) have no project of their own. They
use the results of the projects above, for example `demo` v1 and v2 for
`cmd diff`, and `review-status` for review statuses on the server.
