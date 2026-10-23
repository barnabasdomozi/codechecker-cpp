# Reference: review statuses, source-code comments, rules files, suppress files

Usage: [../usage/review-status-and-suppression.md](../usage/review-status-and-suppression.md)

Python source: `codechecker_common/source_code_comment_handler.py`,
`codechecker_common/review_status_handler.py`,
`analyzer/codechecker_analyzer/suppress_handler.py`,
`suppress_file_handler.py`; server side in
`web/server/codechecker_server/api/mass_store_run.py` and `report_server.py`
(see [store.md](store.md), [cmd-suppress.md](cmd-suppress.md)).

## 1. Values

`unreviewed`, `confirmed`, `false_positive`, `intentional`, `suppress`.
`suppress` is a legacy alias of `false_positive` (comments and rules files
map it; `parse --review-status suppress` is accepted but matches nothing new).

Display form: `_` → space, first letter upper-case, rest lower-case
(`False positive`).

## 2. Determination order (per report)

1. **Source-code comment** (if the main file has not changed since the
   analysis, see [report-format.md](report-format.md) §4). Exactly one
   matching comment → its status, `in_source = true`. More than one →
   error `Multiple source code comments can be found for '<checker>' checker in '<file>' at line <n>.`
   (`parse` exits 1).
2. **Rules file** (`review_status.yaml` in the report folder): the first
   matching rule → its status and reason, `in_source = false`.
3. Otherwise `unreviewed`.

Source file used: `realpath(original path)`, or, when a source root is given
(server-side storage), the path mapped under that root.

## 3. Source-code comment syntax

### Finding comment lines for a report on line `L`

Start at line `L-1` and walk **upwards**:
- a line counts as comment if, stripped, it starts with `//`, or contains
  `/*` or `*/`;
- stop at the first non-comment line (unless inside a C-style comment whose
  end `*/` has been seen);
- stop after processing a line that contains `/*`;
- stop at the top of the file.
Lines are read with undecodable bytes dropped. A quick pre-check skips files
that do not contain `codechecker_` at all.

Collected lines are grouped: whenever a line contains one of the markers
`codechecker_suppress`, `codechecker_false_positive`,
`codechecker_intentional`, `codechecker_confirmed`, the lines collected so
far (from this marker line down to the previous group) form one comment.
The lines are put in top-down order and joined with spaces:
- `//` comments: every `//` is removed;
- C-style: each line stripped, `/*` and `*/` removed, a leading `*`
  removed; joined and stripped.

### Parsing one comment

Collapse whitespace runs into single spaces, then match:

```
^\s*(?P<status>codechecker_suppress|codechecker_false_positive|codechecker_intentional|codechecker_confirmed)\s*\[\s*(?P<checkers>[^\]]*)\s*\]\s*(?P<comment>.*)$
```

- no match → spelling error: the report stays unreviewed and `parse` prints
  `<file> contains misspelled review status comment @<line>: <text>` as a
  warning at the end. This only happens when a line **contains** a full
  marker name but does not parse (verified: `codechecker_suppressss [all]`
  warns; `codechecker_supress [all]` contains no marker, so it is silently
  treated as an ordinary comment).
- `checkers` = `all` → `{"all"}`; else every token matching `[^,\s]+`.
- `comment` → message; empty → `WARNING! source code comment is missing`.
- status: `intentional`, `confirmed`, otherwise (`suppress`,
  `false_positive`) → `false_positive`.
- The stored "line" (printed by `parse` under the report) is the original
  comment text (lines joined with spaces).

### Matching a comment to a report

A comment applies if any of its checker tokens is `all` or is a
**substring** of the report's checker name (so `[core]` also matches
`cppcoreguidelines-...`). A comment is added once per matching token, so a
comment listing two tokens that both match counts as two comments →
ambiguity error.

## 4. Rules file (`review_status.yaml`)

Loaded with a safe YAML loader. A symlink whose target does not exist →
`<target> not found.` A YAML syntax error →
`Invalid YAML format in <file>:\n<err>`.

Validation (each failure is a `ValueError` with the message shown; `analyze`
and `parse` log it and exit 1):

| Check | Message |
|---|---|
| top level is a mapping | `<file> should represent a dictionary.` |
| has `$version` | `<file> must contain the key '$version'. Hint: Currently the latest version of this is 1. Consider adding '$version: 1' to the top of the file.` |
| `$version` is an integer | `<file> should have an integer value for the key '$version'.` |
| version 1: `rules` is a list | `<file> should contain the key 'rules' with a non-empty list of (filters, actions) pair.` |
| each rule is a mapping with `filters` and `actions` | `Format error in <file>. The following section should be an object with keys 'filters' and 'actions':\n<yaml>` |
| `filters` not null | `Format error in <file>: 'filters' must have at least one element.` |
| `actions` not null | `Format error in <file>: 'actions' must have at least one element.` |
| filter keys ⊆ `filepath`, `checker_name`, `report_hash` | `Format error in <file>: filter '<k>' is not allowed. Available filters are: filepath, checker_name, report_hash` |
| action keys ⊆ `review_status`, `reason` | `Format error in <file>: action '<k>' is not allowed. Available actions are: review_status, reason` |
| has `review_status` | `Format error in <file>. 'review_status' is required in:\n<yaml>.` |
| status ∈ `false_positive suppress confirmed intentional` | `Invalid review status field: <v> at <rule> in <file>. Available options are: false_positive, suppress, confirmed, intentional.` |

`suppress` is rewritten to `false_positive`. Other `$version` values are not
validated further.

Matching (all present filters must match). Filter values must be single
strings; they are not type-checked: a list for `filepath` or `report_hash`
crashes the matcher (`TypeError`), a list for `checker_name` never matches:
- `filepath`: shell-glob (`fnmatch`) against the report's **original** path;
- `checker_name`: equality;
- `report_hash`: the report hash **starts with** the value (skipped if the
  report has no hash).

The first matching rule gives the status and `reason` (as bytes; empty if
absent).

(The handler also has an `ignore` action that would drop reports at
analysis time, but `ignore` is not an allowed action, so it can never be
used.)

Where the rules file is read:
- `analyze --review-status-config F`: validated, used only for the (unusable)
  `ignore` action, and copied to `<out>/review_status.yaml`;
- `parse`, `cmd diff` (local folders), `store` (client side, for
  pre-processing) and the server (during storage) read
  `<report dir>/review_status.yaml`.

## 5. Suppress files

Format, matching and writing: [parse.md](parse.md) §6. Import into a
server: [cmd-suppress.md](cmd-suppress.md).
