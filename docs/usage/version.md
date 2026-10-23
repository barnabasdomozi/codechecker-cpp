# Show version information

Three commands print version information. Use them to check which CodeChecker
build is in use, for example in bug reports or CI logs.

| Command | Prints |
|---|---|
| `CodeChecker version` | Analyzer and web package versions together |
| `CodeChecker analyzer-version` | Analyzer package only |
| `CodeChecker web-version` | Web (server/client) package, including Thrift API versions |

All three accept `-o/--output {rows,table,csv,json}`. The default is `table`.

## Print all versions ✅

```sh
CodeChecker version
```

```
[INFO] - CodeChecker analyzer version:
┌──────────────────────┬──────────────────────────────────────────┐
│ Kind                 │ Version                                  │
├──────────────────────┼──────────────────────────────────────────┤
│ Base package version │ 6.30.0                                   │
│ Package build date   │ 2026-10-23T22:59                         │
│ Git commit ID (hash) │ 451b0c5c1786ed21b0685c298f759164a8c5d379 │
│ Git tag information  │ 6.30                                     │
└──────────────────────┴──────────────────────────────────────────┘

[INFO] - CodeChecker web version:
┌─────────────────────────────────────┬─────────────┐
│ Kind                                │ Version     │
├─────────────────────────────────────┼─────────────┤
│ Base package version                │ 6.30.0      │
│ ...                                 │             │
│ Server supported Thrift API version │ 6.74        │
│ Client Thrift API version           │ 6.74        │
└─────────────────────────────────────┴─────────────┘
```

## Machine-readable version ✅

```sh
CodeChecker version -o json
```

```json
{"analyzer": {"base_package_version": "6.30.0", "package_build_date": "2026-10-23T22:59",
              "git_commit": "451b0c5c1786ed21b0685c298f759164a8c5d379", "git_tag": "6.30"},
 "web": {"base_package_version": "6.30.0", "package_build_date": "2026-10-23T22:59",
         "git_commit": "451b0c5c1786ed21b0685c298f759164a8c5d379", "git_tag": "6.30",
         "server_api_version": ["6.74"], "client_api_version": "6.74"}}
```

## Only the analyzer or web package ✅

```sh
CodeChecker analyzer-version -o rows
CodeChecker web-version -o csv
```

```
 Base package version 6.30.0
 Package build date 2026-10-23T22:59
 Git commit ID (hash) 451b0c5c1786ed21b0685c298f759164a8c5d379
 Git tag information 6.30
```

```
Kind,Version
Base package version,6.30.0
Package build date,2026-10-23T22:59
...
```

Exit code: always `0`.
