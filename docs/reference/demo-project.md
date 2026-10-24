# Reference: the demo project as a test fixture

Usage: [../usage/demo-project.md](../usage/demo-project.md)

The demo project (sources on the usage page) is the shared example of the
usage pages. These are the exact results of the reference implementation,
so a port can use them as acceptance tests. Environment: CodeChecker
6.30.0, Clang/Clang-Tidy 19.1.7, GCC 14.2.0, Cppcheck 2.17.1 (no Infer),
default checker profile, `CodeChecker log -b make` + `CodeChecker analyze`
(re-verified 2026-10-24 with Cppcheck; the black-box suite in
[testing/](../../testing/README.md) checks these tables).

Report hashes depend only on file **base names**, line contents, columns,
checker names and messages ([report-hash.md](report-hash.md)), so they are
the same in any checkout directory. Cppcheck's hash does not depend on the
enclosing function, so the two `return a / z;` lines of `style.cpp` get
the same Cppcheck hash.

## 1. `demo` (first version)

| File:line:col | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `main.c:11:12` | clangsa | `core.DivideZero` | HIGH | `09296549497056a329a9ce22dda7d710` | unreviewed |
| `main.c:11:12` | cppcheck | `cppcheck-zerodiv` | HIGH | `04d39a256ef58c412c5acf3f9280fe2e` | unreviewed |
| `main.c:21:7` | clang-tidy | `clang-diagnostic-unused-variable` | MEDIUM | `249a121710b6a5cb467697f323de0d5f` | unreviewed |
| `main.c:25:10` | clang-tidy | `clang-diagnostic-uninitialized` | HIGH | `2e5ec744a6a360431ece6fe858d3f172` | unreviewed |
| `main.c:25:10` | cppcheck | `cppcheck-uninitvar` | HIGH | `3b2c4905253f6d0f7409d271d65dbdd6` | unreviewed |
| `style.cpp:11:12` | clangsa | `core.DivideZero` | HIGH | `21b0090745a8a3abdf9a0989d93a8ce7` | false_positive (`codechecker_suppress`, in source) |
| `style.cpp:11:12` | cppcheck | `cppcheck-zerodiv` | HIGH | `990560afd081e416b9c7cc4c772c4cff` | unreviewed (the comment names `core.DivideZero` only) |
| `style.cpp:17:12` | clangsa | `core.DivideZero` | HIGH | `07a8f6647771d7458a6a8cf8e4b15643` | false_positive (`codechecker_false_positive`, in source) |
| `style.cpp:17:12` | cppcheck | `cppcheck-zerodiv` | HIGH | `990560afd081e416b9c7cc4c772c4cff` | false_positive (`[all]`, in source) |

9 reports in 9 result files (3 sources × clangsa, clang-tidy, cppcheck).
`CodeChecker parse <reports>` (default review-status filter) prints six
reports: the five `main.c` reports and the unreviewed Cppcheck report on
`style.cpp:11`, and exits 2. `main.c:6` (`divide`) and `main.c:16`
(`deref`) are only found with CTU ([ctu-analysis.md](ctu-analysis.md)).

Without Cppcheck (the original environment of the docs) the result is the
five `clangsa`/`clang-tidy` rows, and `parse` prints the three `main.c`
reports of those analyzers.

## 2. `demo2` (second version)

Changes: `main.c:21` → `int *q = 0; *q = 1;`, `style.cpp:4` →
`int *p = 0; int t = 0; (void)(1 / t);`.

| File:line:col | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `main.c:11:12` | clangsa | `core.DivideZero` | HIGH | `09296549497056a329a9ce22dda7d710` | unreviewed |
| `main.c:11:12` | cppcheck | `cppcheck-zerodiv` | HIGH | `04d39a256ef58c412c5acf3f9280fe2e` | unreviewed |
| `main.c:21:16` | cppcheck | `cppcheck-nullPointer` | HIGH | `dc01f09801754018c2543bd48099718f` | unreviewed |
| `main.c:21:18` | clangsa | `core.NullDereference` | HIGH | `19a15bf46b7d3af4b189b3b4f2c28a5b` | unreviewed |
| `main.c:25:10` | clang-tidy | `clang-diagnostic-uninitialized` | HIGH | `2e5ec744a6a360431ece6fe858d3f172` | unreviewed |
| `main.c:25:10` | cppcheck | `cppcheck-uninitvar` | HIGH | `3b2c4905253f6d0f7409d271d65dbdd6` | unreviewed |
| `style.cpp:4:35` | clangsa | `core.DivideZero` | HIGH | `b8956890be49041c8bdd2387242b08d8` | unreviewed |
| `style.cpp:4:35` | cppcheck | `cppcheck-zerodiv` | HIGH | `0983180e376ce9254320b8353442adfa` | unreviewed |
| `style.cpp:11:12` | clangsa | `core.DivideZero` | HIGH | `21b0090745a8a3abdf9a0989d93a8ce7` | false_positive |
| `style.cpp:11:12` | cppcheck | `cppcheck-zerodiv` | HIGH | `990560afd081e416b9c7cc4c772c4cff` | unreviewed |
| `style.cpp:17:12` | clangsa | `core.DivideZero` | HIGH | `07a8f6647771d7458a6a8cf8e4b15643` | false_positive |
| `style.cpp:17:12` | cppcheck | `cppcheck-zerodiv` | HIGH | `990560afd081e416b9c7cc4c772c4cff` | false_positive |

So `demo` → `demo2`: resolved `249a1217...` (unused variable); new
`19a15bf4...`, `b8956890...` (Clang SA), `dc01f098...` and `0983180e...`
(Cppcheck); unchanged the other seven.

## 3. Server expectations

Storing `demo` into an empty product as run `demo`
([store.md](store.md)):
- `runs`: one row, `version = 6.30.0`, `duration` = integer sum of the
  metadata durations (0 for this small project);
- `run_histories.cc_version` = `6.30 (<git hash>)` (from `metadata.json`);
- 9 reports, detection status `new`; the three FP reports have
  `review_status_is_in_source = 1` and `fixed_at = detected_at`;
- `cmd runs -o csv`:
  `demo,6,clangsa (3/3), clang-tidy (3/3), cppcheck (3/3),...` — 6
  unresolved (FP excluded), analyzer statistics `failed+successful /
  successful` per analyzer;
- re-storing `demo2` under the same name: the common hashes become
  `unresolved` (their rows are replaced), `249a1217...` becomes `resolved`
  with `fixed_at` set, the 4 new ones are `new`; `cmd runs` then shows 9
  unresolved reports.
