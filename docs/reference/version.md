# Reference: version commands

Usage: [../usage/version.md](../usage/version.md)

Python source: `codechecker_common/cli_commands/version.py`,
`analyzer/codechecker_analyzer/cli/analyzer_version.py`,
`web/codechecker_web/cli/web_version.py`,
`web/codechecker_web/shared/webserver_context.py`,
`analyzer/codechecker_analyzer/analyzer_context.py`.

## Inputs

Two version files in `<root>/config/`: `analyzer_version.json` and
`web_version.json`. Both have the same format:

```json
{
  "version": {"major": "6", "minor": "30", "revision": "0", "rc": ""},
  "git_hash": "451b0c5c1786ed21b0685c298f759164a8c5d379",
  "git_describe": {"tag": "6.30", "dirty": "6.30-85-g451b0c5c1"},
  "package_build_date": "2026-10-23T22:59"
}
```

The build writes these files (from `git describe` and the build time).

The Thrift API version comes from `codechecker_api/api_version.json`
(`{"api_version": "6.74"}`), shipped with the web part. It is split into
integer major and minor parts (the server uses them for compatibility
checks; see [store.md](store.md)).

## Derived values

| Value | Rule |
|---|---|
| package version | `major.minor.revision`, plus `-rc<rc>` if `rc` is non-empty |
| git tag | `git_describe.tag`, but `git_describe.dirty` when the log level is `DEBUG` or `DEBUG_ANALYZER` |
| git hash | `git_hash` |
| build date | `package_build_date` |
| release candidate | version string contains `rc` |

## Output

### `analyzer-version`

Rows (key, value):

```
Base package version   <version>
Package build date     <date>
Git commit ID (hash)   <hash>
Git tag information    <tag>
```

- `table`/`rows`/`csv`: first logs `CodeChecker analyzer version:` (INFO),
  then prints the two-column table with header `Kind`, `Version` using the
  shared formats ([common-infrastructure.md](common-infrastructure.md) §7).
- `json`: `{"base_package_version", "package_build_date", "git_commit", "git_tag"}`.
- If the version is a release candidate, warn
  `This version is only a release candidate! If you encounter any problems, please submit a bug report!`.
- For any format other than `table`, log lines go to stderr.

### `web-version`

Same, plus two rows:

```
Server supported Thrift API version   <api version>          (list joined with ", ")
Client Thrift API version             <api version>
```

JSON adds `"server_api_version": [<api version>]` (a list) and
`"client_api_version": "<api version>"`. The INFO line is
`CodeChecker web version:`.

### `version`

- `json`: `{"analyzer": <analyzer dict or null>, "web": <web dict or null>}`
  on one line. A part is `null` if its module cannot be loaded (e.g. an
  analyzer-only package).
- Other formats: print the analyzer block, an empty line, then the web
  block. Log lines stay on stdout for all formats here (unlike the two
  single-part commands).

Default format is `table` for all three. Exit code is always 0.
