# Reference: `CodeChecker checkers`

Usage: [../usage/checkers.md](../usage/checkers.md)

Python source: `analyzer/codechecker_analyzer/cli/checkers.py`; data model in
[checker-labels.md](checker-labels.md); checker lists per analyzer in
[analyzer-drivers.md](analyzer-drivers.md#checker-lists).

## Options

| Option | Value | Notes |
|---|---|---|
| `--analyzers A ...` | subset of the 5 names | default: all five |
| `-w`, `--warnings` | flag | deprecated, no effect |
| `--details` | flag | |
| `--label [L]` | optional value | |
| `--profile [P]` | optional value | |
| `--guideline [G]` | optional value | |
| `--severity [S]` | optional value | |
| `--checker-config` | flag | |
| `-o` | `rows`, `table`, `csv`, `json`, `custom` | default `custom` |

The optional-value options are tri-state: absent / given without value /
given with value.

Logging goes to stderr unless `-o table`.

## Dispatch (first match wins)

1. `--profile` without value → list profiles.
2. `--severity` without value → list severities.
3. `--guideline` without value → list guidelines.
4. `--label` without value → list label keys.
5. `--label X` where `X` has no `:` → list values of label `X`.
6. `--checker-config` → list checker options.
7. Otherwise list checkers.

For listings 1–6, `custom` format is treated as `rows`. For `csv`/`json`,
headers are lower-cased with spaces replaced by `_`.

### 1. Profiles

Rows: keys of `descriptions.json["profile"]` in file order. Header
`Profile name`; with `--details` also `Description`.

### 2. Severities

Same with `descriptions.json["severity"]`, header `Severity`
(+ `Description`).

### 3. Guidelines

For each key `g` of `descriptions.json["guideline"]`:
rules = `occurring_values(g)` (values of label `g`, e.g. `sei-cert-c:int33-c`
gives rule `int33-c`), sorted.

- `rows`: for each guideline print two lines:
  `Guideline: <g>` and `Rules: <r1>, <r2>, ...`.
- `json`: `[{"guideline": g, "rules": [sorted list]}, ...]`.
- Other formats: header `Guideline`, `Rules`, rules joined with `, `.

### 4. Label keys

Header `Label`, one row per distinct label key (set order; not sorted).

### 5. Label values

Header `Value`, `occurring_values(X)`. If empty, log INFO
`Label "X" doesn't exist. Use "CodeChecker checkers --label" command to list available labels.`

### 6. Checker options

1. Availability check for `--analyzers` (no exit on error).
2. For every working analyzer, get its checker option list (see
   [analyzer-configuration.md](analyzer-configuration.md)). Rows
   `<analyzer>:<checker>:<option>` (+ documentation with `--details`).
   Header `Option` (+ `Description`).
3. An analyzer with an empty list is reported as a failure (the code tries to
   exempt `cppcheck` and `gcc`, but the conditions make every empty analyzer
   a failure).
4. Print rows (if any), print availability warnings, then if there were
   failures: error
   `Failed to get checker configuration options for '<a, b>' analyzer(s)! Please try to upgrade your analyzer version to use this feature.`
   and exit 1.

### 7. Checker listing

1. Build the label filter list:
   - `--label L` → `L`
   - `--profile P` → `profile:P`
   - `--guideline G` → `guideline:G` if `G` is a value of label `guideline`
     (for the selected analyzers); else if `G` contains no `:` → error
     `--guideline parameter is either <guideline> or <guideline>:<rule>`,
     exit 1; else `G` itself (e.g. `sei-cert-c:str38-c`).
   - `--severity S` → `severity:S`
2. Collect checker info for every **working** analyzer (availability check
   over all five):
   - Get the analyzer's checker list (name, description) from the binary.
   - If `--profile P` is given and `P` is not a key of
     `descriptions.json["profile"]` → error
     `Checker profile 'P' does not exist!` +
     `To list available profiles, use '--profile' without argument.`, exit 1.
   - **Status** is `enabled` if the checker's labels contain
     `profile:default`, else `disabled`. The filter options do not change
     the status.
   - Labels: `labels_of_checker(checker, analyzer)`, sorted by (key, value).
3. For each analyzer in `--analyzers` order: if there is a filter, keep the
   checkers in `checkers_by_labels(filters, analyzer)` (OR of the filters);
   otherwise keep all.
4. `doc_url` values not starting with `http` are made absolute:
   `<root>/www/<value without leading slashes>`.
5. Output:
   - `custom`: for each checker

     ```
     <name>
       Status: enabled|disabled
       Analyzer: <analyzer>
       Description: <description>
       Labels:
         <key>:<value>
         ...
     <empty line>
     ```

     If nothing matched: INFO `No checkers with the given label found.`
   - `json`: with `--details`, a list of
     `{"status", "name", "analyzer", "description", "labels": ["k:v", ...]}`;
     without, a list of names.
   - Other formats: with `--details`, header
     `Status, Name, Analyzer, Description, Labels`, status `+`/`-`, labels
     joined `k:v, k:v`; without, header `Name`. Empty → the INFO message.

Exit code 0 unless an error above.

Note: because checker lists come from the analyzer binaries, an analyzer
that is not installed contributes no checkers even if it has a label file.
