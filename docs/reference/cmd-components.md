# Reference: source components and `CodeChecker cmd components`

Usage: [../usage/cmd-components.md](../usage/cmd-components.md)

Python source: `web/client/codechecker_client/source_component_client.py`,
`web/client/codechecker_client/cli/cmd.py` (`__register_source_components`),
`web/server/codechecker_server/api/report_server.py`
(`addSourceComponent`, `getSourceComponents`, `removeSourceComponent`,
`get_component_values`, `update_source_component_files`,
`get_include_skip_queries`),
`web/server/codechecker_server/api/mass_store_run.py`
(`assign_file_to_source_components`).

All three commands take `--url PRODUCT_URL` (default
`localhost:8001/Default`) and use the results client
([server-api.md](server-api.md) §6).

## 1. Component value

Stored in `source_components(name, value, description, username)`
([server-database.md](server-database.md) §4.2). `value` is the component
file's text with leading/trailing whitespace removed. Interpretation
(`get_component_values`): split on `\n`, strip each line, skip empty lines;
a line starting with `+` adds the rest to the include list, `-` to the
skip list; other lines are ignored. The rest is used **as is** (no
whitespace trimming after the sign).

File membership is kept in `source_component_files` and computed with two
different matchers (rules and the recommended port behavior:
[cmd-results.md](cmd-results.md) §3.1):
- on add/update: SQL `LIKE` with `*` → `%` over all stored file paths;
- when a store inserts a new file: shell-glob matching.

The virtual component `Other (auto-generated)` = files in no component
(used by the report filter); it cannot be added or removed.

## 2. `cmd components list`

```
CodeChecker cmd components list [-o {plaintext,rows,table,csv,json}] [--url PRODUCT_URL]
```

Logging to stderr unless `table`. `getSourceComponents(null)`.

- `json`: list of `{"name", "value", "description"}` (one line); the last
  entry is `{"name": "Other (auto-generated)", "value": null,
  "description": "Special auto-generated source component which contains
  files that are uncovered by the rest of the components."}`.
- others: header `Name, Value, Description`; one row per line of the value
  (`\n`-split): name and description only on the first row (description
  empty if null); `Other` has an empty value.

Server `getSourceComponents(filter)` (needs view permission): components
ordered by name. If `filter` is a non-empty list, every element becomes
`name ILIKE <element with * → %>` and the conditions are **AND**-ed (so a
multi-element filter matches only names fitting all patterns). `Other` is
appended when there is no filter or the filter contains its exact name.

## 3. `cmd components add`

```
CodeChecker cmd components add NAME -i/--import COMPONENT_FILE
                               [--description TEXT] [--url PRODUCT_URL]
```

1. `NAME` = `Other (auto-generated)` → ERROR `'Other (auto-generated)' is a
   special virtual component which can not be modified.`, exit 1.
2. Read the file (UTF-8, errors ignored), strip it. A missing file is an
   uncaught `FileNotFoundError` (traceback, exit 1).
3. `getSourceComponents([NAME])` (an ILIKE pattern, so `_`, `%` and case
   differences also match). Non-empty → INFO `The source component '<NAME>'
   already exist!` and prompt `Do you want to update? Y(es)/n(o) ` on stdout
   (answer `y`/`yes`, case-insensitive). Other answers → INFO `No source
   component update was done.`, exit 0. End of input → `EOFError` traceback,
   exit 1.
4. `addSourceComponent(NAME, value, TEXT or null)`; true → INFO `Source
   component added.`, exit 0; false → ERROR `An error occurred when adding
   source component.`, exit 1.

Server `addSourceComponent(name, value, description)` (needs
`PRODUCT_ADMIN`): look up the row by exact name; update `value` and
`description` (the user name is meant to be updated too but the code sets
a wrong attribute, so `username` keeps the creator), or insert
`(name, value, description, username = caller or NULL)`; recompute its
`source_component_files` rows (delete them, then insert the matching file
ids: include-matches minus skip-matches; only skip patterns → all files
except the skip-matches; no patterns → none); commit; return true.

## 4. `cmd components del`

```
CodeChecker cmd components del NAME [--url PRODUCT_URL]
```

1. `Other (auto-generated)` → ERROR `'Other (auto-generated)' is a special
   virtual component which can not be removed.`, exit 1.
2. `getSourceComponents([NAME])` empty → ERROR `The source component
   '<NAME>' does not exist!`, exit 1.
3. `removeSourceComponent(NAME)`: true → INFO `Source component removed.`,
   exit 0; false → ERROR `An error occurred in source component removal.`,
   exit 1.

Server `removeSourceComponent(name)` (needs `PRODUCT_ADMIN`): delete by
exact name (its file associations cascade), INFO `Source component '<n>'
has been removed by '<user>'`, return true; not found → `DATABASE "Source
component <name> was not found in the database."`
