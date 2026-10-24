# Reference: the demo project as a test fixture

Usage: [../usage/demo-project.md](../usage/demo-project.md)

The demo project (sources on the usage page) is the shared example of the
usage pages. These are the exact results of the reference implementation,
so a port can use them as acceptance tests. Environment: CodeChecker
6.30.0, Clang/Clang-Tidy 19.1.7, GCC 14.2.0 (no Cppcheck/Infer), default
checker profile, `CodeChecker log -b make` + `CodeChecker analyze`
(re-verified 2026-10-24).

Report hashes depend only on file **base names**, line contents, columns,
checker names and messages ([report-hash.md](report-hash.md)), so they are
the same in any checkout directory.

## 1. `demo` (first version)

| File:line:col | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `main.c:11:12` | clangsa | `core.DivideZero` | HIGH | `09296549497056a329a9ce22dda7d710` | unreviewed |
| `main.c:21:7` | clang-tidy | `clang-diagnostic-unused-variable` | MEDIUM | `249a121710b6a5cb467697f323de0d5f` | unreviewed |
| `main.c:25:10` | clang-tidy | `clang-diagnostic-uninitialized` | HIGH | `2e5ec744a6a360431ece6fe858d3f172` | unreviewed |
| `style.cpp:11:12` | clangsa | `core.DivideZero` | HIGH | `21b0090745a8a3abdf9a0989d93a8ce7` | false_positive (`codechecker_suppress`, in source) |
| `style.cpp:17:12` | clangsa | `core.DivideZero` | HIGH | `07a8f6647771d7458a6a8cf8e4b15643` | false_positive (`codechecker_false_positive`, in source) |

`CodeChecker parse <reports>` (default review-status filter) prints the
three `main.c` reports and exits 2. `main.c:6` (`divide`) and `main.c:16`
(`deref`) are only found with CTU ([ctu-analysis.md](ctu-analysis.md)).

## 2. `demo2` (second version)

Changes: `main.c:21` → `int *q = 0; *q = 1;`, `style.cpp:4` →
`int *p = 0; int t = 0; (void)(1 / t);`.

| File:line:col | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `main.c:11:12` | clangsa | `core.DivideZero` | HIGH | `09296549497056a329a9ce22dda7d710` | unreviewed |
| `main.c:21:18` | clangsa | `core.NullDereference` | HIGH | `19a15bf46b7d3af4b189b3b4f2c28a5b` | unreviewed |
| `main.c:25:10` | clang-tidy | `clang-diagnostic-uninitialized` | HIGH | `2e5ec744a6a360431ece6fe858d3f172` | unreviewed |
| `style.cpp:4:35` | clangsa | `core.DivideZero` | HIGH | `b8956890be49041c8bdd2387242b08d8` | unreviewed |
| `style.cpp:11:12` | clangsa | `core.DivideZero` | HIGH | `21b0090745a8a3abdf9a0989d93a8ce7` | false_positive |
| `style.cpp:17:12` | clangsa | `core.DivideZero` | HIGH | `07a8f6647771d7458a6a8cf8e4b15643` | false_positive |

So `demo` → `demo2`: resolved `249a1217...` (unused variable); new
`19a15bf4...` and `b8956890...`; unchanged the other four.

## 3. Server expectations

Storing `demo` into an empty product as run `demo`
([store.md](store.md)):
- `runs`: one row, `version = 6.30.0`, `duration` = integer sum of the
  metadata durations (0 for this small project);
- `run_histories.cc_version` = `6.30 (<git hash>)` (from `metadata.json`);
- 5 reports, detection status `new`; the two FP reports have
  `review_status_is_in_source = 1` and `fixed_at = detected_at`;
- `cmd runs -o csv`: `demo,3,clangsa (3/3), clang-tidy (3/3),...` — 3
  unresolved (FP excluded), analyzer statistics `failed+successful /
  successful` per analyzer;
- re-storing `demo2` under the same name: the 4 common hashes become
  `unresolved` (their rows are replaced), `249a1217...` becomes `resolved`
  with `fixed_at` set, the 2 new ones are `new`.
