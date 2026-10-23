# Checker labels and guidelines (shared data model)

Used by `checkers`, checker selection in `analyze`/`check`, severity lookup
in report files, and the server.

Python source: `codechecker_common/checker_labels.py`,
`codechecker_common/guidelines.py`.

## Label files

Folder: `<root>/config/labels/` (overridable for tests with
`CC_TEST_LABELS_DIR`).

- `analyzers/<file>.json`, one per tool:

  ```json
  {
    "analyzer": "clangsa",
    "labels": {
      "core.DivideZero": [
        "doc_url:https://clang.llvm.org/docs/analyzer/checkers.html#core-dividezero-c",
        "guideline:sei-cert-c",
        "profile:default",
        "profile:extreme",
        "profile:security",
        "profile:sensitive",
        "sei-cert-c:int33-c",
        "severity:HIGH"
      ],
      ...
    }
  }
  ```

  Files exist for the five analyzers and for report-converter tools (`asan`,
  `pylint`, ...). The file name is not used; the `analyzer` field is the key.
- `descriptions.json`: allowed values and descriptions of three labels:

  ```json
  {"profile":   {"default": "...", "sensitive": "...", "security": "...", "portability": "...", "extreme": "..."},
   "severity":  {"CRITICAL": "...", "HIGH": "...", "MEDIUM": "...", "LOW": "...", "STYLE": "...", "UNSPECIFIED": "..."},
   "guideline": {"cwe-top-25-2024": "...", "sei-cert-c": "...", "misra-c": "...", ...}}
  ```

  Key order in this file is the listing order of `checkers --profile`,
  `--severity` and `--guideline`.

## Label syntax

A label is `key:value`. Split at the **first** colon; both sides are
whitespace-trimmed. Every label must contain a colon (otherwise the label
files are rejected with a `ValueError` at start-up).

Unique labels (at most one per checker) and their defaults when missing:

| Label | Default |
|---|---|
| `severity` | `UNSPECIFIED` |
| `blacklist` | `false` |
| `description` | empty |

Common labels: `severity`, `profile`, `guideline`, `<guideline>` (rule ids,
e.g. `sei-cert-c:int33-c`), `doc_url`, `blacklist`, `description`,
`label-tool-skip`, `cwe-vulnerabilities`, ...

## Data model

`analyzer → checker → [ (key, value), ... ]`, built from all files. Each
file's `labels` object keeps its JSON key order (this matters for prefix
lookup below).

Operations:

| Operation | Definition |
|---|---|
| `checkers(analyzer?)` | All checker names (of one or all analyzers). |
| `labels(analyzer?)` | Set of label keys used anywhere. |
| `occurring_values(key, analyzer?)` | Set of values used with `key`. |
| `checkers_by_labels(labels, analyzer?)` | Checkers that have **at least one** of the given `key:value` pairs (OR). Searches all analyzers if none given. |
| `labels_of_checker(checker, analyzer?)` | The checker's label pairs. If the exact name is not a key, the **first key (in file order) that is a prefix** of the name is used, e.g. an entry `clang-diagnostic` matches `clang-diagnostic-unused-argument`. Results from several analyzers are merged and de-duplicated. |
| `label_of_checker(checker, key, analyzer?)` | For unique keys: the value or the default. Otherwise: the list of values. |
| `severity(checker, analyzer?)` | `label_of_checker(checker, "severity")`. |
| `get_description(key)` | The `descriptions.json` object for `key`. |

### Severity of compiler diagnostics (server side)

The web server's severity map adds a fallback: an unknown
`clang-diagnostic-error` is `CRITICAL`, any other unknown
`clang-diagnostic-*` is `MEDIUM`, everything else unknown is `UNSPECIFIED`.

## Guideline files

`<root>/config/guidelines/*.yaml` (files ending in `.license` ignored):

```yaml
guideline: sei-cert-c
guideline_title: SEI CERT Coding Standard (C)
rules:
- rule_id: arr30-c
  rule_url: https://...
  title: ...            # optional
```

Validation: `guideline` and `guideline_title` must be strings, `rules` a list
of objects each with a string `rule_id`. Invalid files are skipped with a
warning. Model: `guideline → rule_id → {other fields}`.

The command-line `checkers --guideline` listing does **not** use these YAML
files; it uses label values (see [checkers.md](checkers.md)). The YAML files
are used by the web server (guideline statistics).
