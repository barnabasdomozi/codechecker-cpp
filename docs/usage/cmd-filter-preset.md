# Saved result filters (`cmd filter-preset`)

A filter preset is a named, saved set of result filters stored on the server
(per product). Use it with `--filter-preset NAME` in `cmd results`,
`cmd sum` and `cmd diff`, and in the web UI.

## Create a preset ✅

```sh
CodeChecker cmd filter-preset new --name high_core --severity high --checker-name "core.*" \
  --url localhost:8555/Default
```

```
...
  Severity: HIGH
  Is unique: no
============================================================
```

`new` takes every filter option of [`cmd results`](cmd-results.md).

## List presets ✅

```sh
CodeChecker cmd filter-preset list --url localhost:8555/Default
CodeChecker cmd filter-preset list -o csv --url localhost:8555/Default
CodeChecker cmd filter-preset list -o json --url localhost:8555/Default
```

```
┌────┬───────────┬────────────────────────────────────────┐
│ ID │ Name      │ Active Filters                         │
├────┼───────────┼────────────────────────────────────────┤
│ 1  │ high_core │ checkerName(1), severity(1), uniqueing │
└────┴───────────┴────────────────────────────────────────┘
```

```
ID,Name,Active Filters
1,core_high,checkerName(1), severity(1), uniqueing
```

## Use a preset ✅

```sh
CodeChecker cmd results demo --filter-preset high_core --url localhost:8555/Default
```

```
│ main.c @ 21    │ core.NullDereference │ HIGH │ ... │ UNREVIEWED     │ NEW        │
│ style.cpp @ 4  │ core.DivideZero      │ HIGH │ ... │ UNREVIEWED     │ NEW        │
│ style.cpp @ 11 │ core.DivideZero      │ HIGH │ ... │ FALSE_POSITIVE │ UNRESOLVED │
│ style.cpp @ 17 │ core.DivideZero      │ HIGH │ ... │ FALSE_POSITIVE │ UNRESOLVED │
```

⚠️ The preset replaces **all** filters, including the default review-status
filter, so false positives are listed too.

## Rename a preset ✅

```sh
CodeChecker cmd filter-preset rename --preset-id 1 --new-name core_high --url localhost:8555/Default
```

```
[INFO] - Filter preset (ID: 1) renamed to 'core_high'.
```

## Delete a preset ✅

```sh
CodeChecker cmd filter-preset delete --id 2 --url localhost:8555/Default
```

```
[INFO] - Filter preset 'tmp' (ID: 2) successfully deleted.
```

Note the different option names: `rename --preset-id`, but `delete --id`.
