# Review status and suppression

Every report has a **review status**:

| Status | Meaning | Shown by `parse` by default |
|---|---|---|
| `unreviewed` | Nobody has looked at it yet | yes |
| `confirmed` | It is a real bug | yes |
| `false_positive` | Not a bug | no |
| `intentional` | A bug, but it will not be fixed | no |
| `suppress` | Older name, treated as false positive | no |

There are four ways to set it:

1. Comments in the source code (local and server).
2. A `review_status.yaml` rules file passed to `analyze`.
3. A suppress file (`parse --suppress`, `cmd suppress`).
4. In the web UI or through the server API (server only).

## 1. Source-code comments ✅

Put the comment on the line(s) directly above the reported line:

```cpp
int suppressed(int a) {
  int z = 0;
  // codechecker_suppress [core.DivideZero] intentional for the demo
  return a / z;
}

int false_pos(int a) {
  int z = 0;
  // codechecker_false_positive [all] demo comment
  return a / z;
}
```

Comment forms:

| Comment | Status |
|---|---|
| `// codechecker_false_positive [checkers] reason` | false_positive |
| `// codechecker_intentional [checkers] reason` | intentional |
| `// codechecker_confirmed [checkers] reason` | confirmed |
| `// codechecker_suppress [checkers] reason` | false_positive (shown as `[False positive]`) |

- `[checkers]` is a comma-separated list such as
  `[core.DivideZero, deadcode.DeadStores]`, or `[all]` for every checker.
  A checker name prefix also matches, e.g. `[core]` ✅.
- C-style comments (`/* codechecker_false_positive [all] reason */`) ✅ and
  reasons continued on following `//` lines ✅ work too.
- Several comments can be stacked above one line for different checkers ✅.

All of these forms were tested on one file:

```c
int f1(int a) { int z = 0;
  // codechecker_intentional [core.DivideZero] keep it
  return a / z; }                                     // -> [Intentional]
int f3(int a) { int z = 0;
  /* codechecker_false_positive [all] c style */
  return a / z; }                                     // -> [False positive]
int f5(int a) { int z = 0;
  // codechecker_false_positive [core.DivideZero] this is a long
  // comment over two lines
  return a / z; }                                     // -> [False positive]
int f6(int a) { int z = 0; int u;
  // codechecker_intentional [core.DivideZero] stacked one
  // codechecker_confirmed [clang-diagnostic-uninitialized] stacked two
  return a / z + u; }   // -> DivideZero [Intentional], uninitialized [Confirmed]
```

`confirmed` reports are still shown by default, with `[Confirmed]` at the end
of the line.

With the demo project, `parse` hides both `style.cpp` reports. To see them:

```sh
CodeChecker parse ./reports --review-status false_positive
```

```
[HIGH] ~/demo/style.cpp:11:12: Division by zero [core.DivideZero] [False positive]
[HIGH] ~/demo/style.cpp:17:12: Division by zero [core.DivideZero] [False positive]
```

When stored to a server, these reports get the review status from the comment,
with `isInSource: true`, and it cannot be changed in the web UI.

## 2. Review status rules file (`--review-status-config`) ✅

Set review statuses by file path, checker name or report hash, without
changing the sources. Pass the file to `analyze` or `check`:

`review_status.yaml`:

```yaml
$version: 1
rules:
  - filters:
      filepath: "*/main.c"
      checker_name: clang-diagnostic-unused-variable
    actions:
      review_status: intentional
      reason: Kept for demonstration
```

```sh
CodeChecker analyze compile_commands.json -o reports --review-status-config review_status.yaml
CodeChecker parse reports                                  # unused-variable report is hidden
CodeChecker parse reports --review-status intentional
```

```
[MEDIUM] ~/demo/main.c:21:7: unused variable 'unused' [clang-diagnostic-unused-variable] [Intentional]
```

Filters: `filepath` (glob), `checker_name`, `report_hash` ✅. All filters in
one rule must match. A rule by hash:

```yaml
$version: 1
rules:
  - filters:
      report_hash: d092ada5ae505769e0d7607e4877cce0
    actions:
      review_status: false_positive
      reason: by hash
``` Actions: `review_status` and `reason`. The status is written
into the result files, so `parse` and `store` both see it.

## 3. Suppress files ✅

A suppress file has one line per report:

```
<report hash>||<file name>||<comment>||<status>
```

### Create one from the source-code comments

```sh
CodeChecker parse ./reports --export-source-suppress --suppress suppress.txt
```

```
21b0090745a8a3abdf9a0989d93a8ce7||style.cpp||intentional for the demo||false_positive
07a8f6647771d7458a6a8cf8e4b15643||style.cpp||demo comment||false_positive
```

### Hide reports with it

```sh
CodeChecker parse ./reports --suppress suppress.txt
CodeChecker check -l compile_commands.json --suppress suppress.txt
```

Reports whose hash is in the file are hidden like source-code suppressions.

### Import it into a server

```sh
CodeChecker cmd suppress my_run -i suppress.txt --url localhost:8001/Default
```

See [cmd-suppress.md](cmd-suppress.md).

## 4. Server-side review status

On a server, review statuses can also be set in the web UI. They can be
copied between servers with `cmd export` / `cmd import` (see
[cmd-export-import.md](cmd-export-import.md)). Filter by review status with
`cmd results --review-status`, `cmd sum` and `cmd diff`.
