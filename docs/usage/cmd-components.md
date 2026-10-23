# Source components (`cmd components`)

A source component is a named set of path patterns, e.g. "everything in
`lib/` except `lib/skip/`". Components are used to filter results
(`--component` in `cmd results`, `cmd sum`, `cmd diff`, and in the web UI).
They belong to a product.

## Add or update a component ✅

Component file `comp.txt`: one pattern per line, `+` to include, `-` to
exclude, `*` wildcards:

```
+*/lib/*
-*/lib/skip/*
```

```sh
CodeChecker cmd components add libcomp -i comp.txt --description "Library code" --url localhost:8555/Default
```

```
[INFO] - Source component added.
```

Running `add` again with an existing name asks before replacing ✅:

```sh
echo y | CodeChecker cmd components add maincomp -i comp3.txt --url localhost:8555/Default
```

```
[INFO] - The source component 'maincomp' already exist!
Do you want to update? Y(es)/n(o) [INFO] - Source component added.
```

⚠️ Without input (e.g. in CI with stdin closed), the prompt crashes with
`EOFError: EOF when reading a line` and the component is not changed.

## List components ✅

```sh
CodeChecker cmd components list --url localhost:8555/Default
CodeChecker cmd components list -o json --url localhost:8555/Default
```

```
┌────────────────────────┬───────────────┬────────────────────────────────────────────────────────────────────────────────────┐
│ Name                   │ Value         │ Description                                                                        │
├────────────────────────┼───────────────┼────────────────────────────────────────────────────────────────────────────────────┤
│ libcomp                │ +*/lib/*      │ Library code                                                                       │
│                        │ -*/lib/skip/* │                                                                                    │
│ Other (auto-generated) │               │ Special auto-generated source component which contains files that are uncovered... │
└────────────────────────┴───────────────┴────────────────────────────────────────────────────────────────────────────────────┘
```

```json
[{"name": "maincomp", "value": "+*/main.c", "description": null},
 {"name": "Other (auto-generated)", "value": null,
  "description": "Special auto-generated source component which contains files that are uncovered by the rest of the components."}]
```

`Other (auto-generated)` is always present and matches files that no other
component covers.

## Filter results by component ✅

```sh
printf '+*/main.c\n' > comp2.txt
CodeChecker cmd components add maincomp -i comp2.txt --url localhost:8555/Default
CodeChecker cmd results multi --component maincomp --url localhost:8555/Default
```

Shows the reports in `.../main.c`.

⚠️ Patterns are matched against the **stored** paths. In a run stored with
`--trim-path-prefix`, paths look like `main.c`, so `*/main.c` matches nothing
(the test run `demo` returned no results). After changing the pattern to
`+*main.c`, `cmd results demo --component maincomp` lists the `main.c`
reports ✅.

## Delete a component ✅

```sh
CodeChecker cmd components del libcomp --url localhost:8555/Default
```

```
[INFO] - Source component removed.
```
