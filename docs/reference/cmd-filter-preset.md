# Reference: filter presets and `CodeChecker cmd filter-preset`

Usage: [../usage/cmd-filter-preset.md](../usage/cmd-filter-preset.md)

Python source: `web/client/codechecker_client/filter_preset_client.py`,
`web/client/codechecker_client/cli/cmd.py` (`__register_filter_presets`,
`__add_filtering_arguments`),
`web/client/codechecker_client/cmd_line_client.py`
(`parse_report_filter_offline`, `parse_report_filter`),
`web/server/codechecker_server/api/report_server.py`
(`storeFilterPreset`, `renameFilterPreset`, `deleteFilterPreset`,
`getFilterPreset`, `listFilterPreset`, `transform_rf_db_to_thrift`),
`codechecker_common/util.py` (`thrift_to_json`).

All subcommands take `--url PRODUCT_URL` (default `localhost:8001/Default`)
and use the results client ([server-api.md](server-api.md) §6). Using a
preset in `cmd results/sum/diff` (`--filter-preset`) is described in
[cmd-diff.md](cmd-diff.md) §5.

## 1. Storage

Table `filter_presets(id, preset_name UNIQUE, report_filter JSON)`
([server-database.md](server-database.md) §4.2). The stored value is a
**JSON string containing JSON** (double-encoded): the `ReportFilter` is
converted with `thrift_to_json` (struct → object of its non-null fields,
recursively; enums as integers), dumped to text, and that text is stored
in the JSON column. Example row:

```
"{\"checkerName\": [\"core.*\"], \"severity\": [40], \"isUnique\": false, \"date\": {\"detected\": {\"after\": 1767222000}}, \"fileMatchesAnyPoint\": false, \"componentMatchesAnyPoint\": false, \"fullReportPathInComponent\": false}"
```

Reading back (`transform_rf_db_to_thrift`): parse the inner JSON and build
a `ReportFilter` from the keys `filepath, checkerMsg, checkerName,
reportHash, severity, reviewStatus, detectionStatus, runHistoryTag,
firstDetectionDate, fixDate, isUnique, runName, runTag, componentNames,
bugPathLength{min,max}, date{detected{before,after}, fixed{before,after}},
analyzerNames, openReportsDate, cleanupPlanNames, fileMatchesAnyPoint,
componentMatchesAnyPoint, annotations[{first,second}], reportStatus,
fullReportPathInComponent` — every field of the struct; missing keys are
null, except `annotations`, which is always a list (`[]` if absent).

## 2. `filter-preset new`

```
CodeChecker cmd filter-preset new --name PRESET_NAME [filter options] [--url PRODUCT_URL]
```

Filter options as in [cmd-results.md](cmd-results.md) §2, but **without
defaults**: options not given are absent (so the preset has no review or
detection status filter unless given). `--tag` is accepted but ignored
(the offline filter makes no server calls).

1. Build the filter offline ([local-diff.md](local-diff.md) §5.1);
   `isUnique` = `--uniqueing on` (so always set, `false` by default);
   `fileMatchesAnyPoint`, `componentMatchesAnyPoint`,
   `fullReportPathInComponent` always set (false by default). Same warnings
   as in `cmd results`.
2. `listFilterPreset()`; a preset with exactly this name → ERROR `Filter
   preset '<n>' already exists. Use a different name or delete the existing
   preset to create a new one.`, exit 1.
3. `storeFilterPreset(FilterPreset(id=-1, name, filter))` → id. INFO
   `Filter preset '<n>' saved with ID: <id>`, INFO `Filter preset '<n>'
   created successfully.`
4. `getFilterPreset(id)` and print (stdout):

   ```

   Filter Preset Created/Updated:
   ============================================================
   ID:          <id>
   Name:        <name>

   Filter Configuration:
     <one line per set field, see below>
   ============================================================
   ```

   Lines, in this order, each only if the field is non-empty/true:
   `File paths: a, b`, `Checker names: ...`, `Checker messages: ...`,
   `Report hashes: ...`, `Severity: HIGH, ...`, `Review status: ...`,
   `Detection status: ...` (enum names), `Tags: ...`, `Components: ...`,
   `Analyzer names: ...`, `Bug path length: <min or any>:<max or any>`,
   `Is unique: yes|no` (whenever not null), `Open reports date:
   YYYY-MM-DD HH:MM:SS`, `Detected date range:` with `    After: YYYY-MM-DD`
   / `    Before: ...`, `Fixed date range:` likewise (local time), `Run
   names: ...`, `Run tags: ...`, `Cleanup plans: ...`, `File matches any
   point: yes`, `Component matches any point: yes`, `Annotation: k=v`,
   `Report status: ...`, `Full report path in component: yes`. If none of
   the fields except `isUnique` is set: `  (no filters configured)`. Each
   line is indented by two spaces.

Errors from the store call → exit 1 (via the call wrapper; other exceptions
→ ERROR `An error occurred while saving the filter preset: <e>`, exit 1).

Server `storeFilterPreset(preset)` (needs `PRODUCT_ADMIN`): name trimmed.
`id == -1`: name exists → error; else insert and return the new id.
Otherwise update the preset with that id (name and filter); unknown id →
error. Every error is re-wrapped as `DATABASE "CodeChecker could not store
the filter preset: <inner>"`, where `<inner>` is the textual form of the
original exception (for the duplicate case:
`RequestFailed(errorCode=0, message="A filter preset with name '<n>' already exists!", extraInfo=None)`).

## 3. `filter-preset list`

```
CodeChecker cmd filter-preset list [-o {plaintext,rows,table,csv,json}] [--url PRODUCT_URL]
```

Logging to stderr unless `table`. `listFilterPreset()` (needs view
permission; all presets, database order). None → INFO `No filter presets
found.`, exit 0.

- `json`: indented by 2:
  `[{"id", "name", "filters": <thrift_to_json of the filter>}]` with
  `severity`, `reviewStatus`, `detectionStatus`, `reportStatus` lists
  converted to enum names (`"HIGH"`).
- others: header `ID, Name, Active Filters`; the summary joins with `", "`
  the active entries in this order: `filepath(n)`, `checkerName(n)`,
  `checkerMsg(n)`, `reportHash(n)`, `severity(n)`, `reviewStatus(n)`,
  `detectionStatus(n)`, `tag(n)`, `components(n)`, `analyzers(n)`,
  `bugPathLength`, `dateRange`, `uniqueing` (whenever `isUnique` is not
  null, i.e. always for CLI-made presets), `runName(n)`, `runTag(n)`,
  `openReportsDate`, `cleanupPlans(n)`, `fileMatchesAnyPoint`,
  `componentMatchesAnyPoint`, `annotations(n)`, `reportStatus(n)`,
  `fullReportPathInComponent`; nothing → `(none)`. (`n` = list length.)

## 4. `filter-preset rename`

```
CodeChecker cmd filter-preset rename --preset-id ID --new-name NAME [--url PRODUCT_URL]
```

`renameFilterPreset(ID, NAME)` → INFO `Filter preset (ID: <id>) renamed to
'<name>'.`, exit 0. Server (needs `PRODUCT_ADMIN`): name trimmed; empty →
`Preset name cannot be empty!`; id 0 → `Preset ID cannot be empty!`;
unknown id → `No filter preset found with id <id>!`; another preset has
the name → `A filter preset with name '<n>' already exists!`. All wrapped
as `DATABASE "CodeChecker could not rename filter preset: <inner>"`
(verified) → exit 1.

## 5. `filter-preset delete`

```
CodeChecker cmd filter-preset delete --id ID [--url PRODUCT_URL]
```

1. `listFilterPreset()`; no preset with that id → ERROR `Filter preset with
   ID <id> does not exist!`, exit 1.
2. `deleteFilterPreset(ID)` → returns the id (truthy) → INFO `Filter preset
   '<name>' (ID: <id>) successfully deleted.`, exit 0. (A returned 0 would
   be treated as failure: ERROR `An error occurred when deleting the filter
   preset.`, exit 1.)

Server (needs `PRODUCT_ADMIN`): unknown id → `DATABASE "No filter preset
found with id <id>!"`.
