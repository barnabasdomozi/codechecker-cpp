# List checkers

`CodeChecker checkers` lists the checkers of every analyzer, whether each is
enabled by default, and the labels attached to it (severity, profile,
guideline, documentation URL, ...). Use it to find checker names for
`analyze -e/-d` (see [checker-selection.md](checker-selection.md)).

```
CodeChecker checkers [--analyzers ANALYZER ...] [--details]
                     [--label [LABEL]] [--profile [PROFILE]]
                     [--guideline [GUIDELINE]] [--severity [SEVERITY]]
                     [--checker-config] [-w]
                     [-o {rows,table,csv,json,custom}]
```

Labels are stored in `config/labels/*.json` in the CodeChecker installation.
Editing the `profile:default` labels there changes which checkers are on by
default.

## List all checkers ✅

```sh
CodeChecker checkers
```

The default output format is `custom`:

```
alpha.clone.CloneChecker
  Status: disabled
  Analyzer: clangsa
  Description: (Enable only for development!) Reports similar pieces of code.
  Labels:
    doc_url:https://clang.llvm.org/docs/analyzer/checkers.html#alpha-clone-clonechecker-c-c-objc
    ...
```

## Checkers of some analyzers only ✅

```sh
CodeChecker checkers --analyzers clang-tidy -o table
CodeChecker checkers --analyzers clangsa gcc
```

```
┌──────────────────────────┐
│ Name                     │
├──────────────────────────┤
│ abseil-cleanup-ctad      │
│ abseil-duration-addition │
│ ...                      │
```

## Detailed list ✅

```sh
CodeChecker checkers --details --analyzers clangsa -o json
```

```json
[{"status": "disabled", "name": "alpha.clone.CloneChecker", "analyzer": "clangsa",
  "description": "(Enable only for development!) Reports similar pieces of code.",
  "labels": [...]}, ...]
```

`--details` adds the status, analyzer, severity, guidelines and description
in every output format.

## Output formats ✅

`-o rows` (names only), `-o table`, `-o csv`, `-o json`, `-o custom` (default).

```sh
CodeChecker checkers --profile sensitive -o csv
```

```
name
core.BitwiseShift
core.CallAndMessage
core.DivideZero
...
```

## Browse and filter by label ✅

```sh
CodeChecker checkers --label                      # list label keys
CodeChecker checkers --label severity             # list the values of one label
CodeChecker checkers --label severity:HIGH -o rows
CodeChecker checkers --label sei-cert-c:str34-c
```

```
 severity
 label-tool-skip
 misra-c
 guideline
 cwe-vulnerabilities
 owasp-top-10-2025
 ...
```

```
 HIGH
 CRITICAL
 LOW
 UNSPECIFIED
 MEDIUM
 STYLE
```

## Profiles ✅

```sh
CodeChecker checkers --profile             # default, sensitive, security, portability, extreme
CodeChecker checkers --profile sensitive   # same as --label profile:sensitive
```

## Guidelines ✅

```sh
CodeChecker checkers --guideline                     # every guideline and its rules
CodeChecker checkers --guideline sei-cert-c          # checkers for all SEI CERT C rules
CodeChecker checkers --guideline sei-cert-c:str34-c  # checkers for one rule
```

```
Guideline: cwe-top-25-2024
Rules: cwe-119, cwe-125, cwe-190, ...
...
```

```
bugprone-signed-char-misuse
  Status: enabled
  Analyzer: clang-tidy
  ...
```

## Severities ✅

```sh
CodeChecker checkers --severity            # CRITICAL HIGH MEDIUM LOW STYLE UNSPECIFIED
CodeChecker checkers --severity CRITICAL --analyzers clang-tidy
```

Severity values must be in capitals.

## Checker configuration options ✅

```sh
CodeChecker checkers --checker-config
```

```
 clangsa:alpha.clone.CloneChecker:MinimumCloneComplexity
 clangsa:alpha.clone.CloneChecker:ReportNormalClones
 ...
```

These options are passed to `analyze --checker-config` as
`<analyzer>:<checker>:<option>=<value>`
(see [analyzer-configuration.md](analyzer-configuration.md)).

## Deprecated: `-w/--warnings` ✅

`-w` is deprecated. In 6.30.0 it prints the same list as plain
`CodeChecker checkers`.

Exit code: `0`.
