# Report hashes (shared)

A report hash identifies "the same finding" across analyses. It drives
`cmd diff`, baselines, suppress files, review status rules, detection status
on the server, and uniqueing.

Python source:
`tools/report-converter/codechecker_report_converter/report/hash.py`.
Which hash each analyzer uses: [analyzer-drivers.md](analyzer-drivers.md) §6.

## Common rules

- The hash is `md5_hex( "|||".join(parts) )`, with the joined string encoded
  as UTF-8 (unencodable characters dropped).
- `file name` = base name of the report's main file (original path).
- `line content` = the source line read from disk as UTF-8 with undecodable
  bytes **dropped**, **including** its line terminator (`\n`), or `""` if the
  line does not exist / the file cannot be read. (If the file content was
  provided in memory, e.g. from a server, the line is taken from that
  content with its line ending kept.)
- `static message` = the report message, unless a converter provided a more
  stable message for hashing (sanitizer parsers strip addresses).
- If computing the parts throws, an error is logged and the hash of the empty
  string (`d41d8cd98f00b204e9800998ecf8427e`) results.

## Path-sensitive (default for non-Clang-SA analyzers)

```
parts = [ file name,
          checker name,
          static message,
          line content of the LAST bug path event (its file and line),
          str(column of the last event),
          str(column of the last event),
          for each bug path position that has a range:
              str(range.start_col), str(range.end_col) ]
```

Verified example (clang-tidy `clang-diagnostic-unused-variable` at
`main.c:21:7`, line `  int unused = 5;`):
`md5("main.c|||clang-diagnostic-unused-variable|||unused variable 'unused'|||  int unused = 5;\n|||7|||7")`
= `249a121710b6a5cb467697f323de0d5f`.

When reading a plist whose diagnostic has no
`issue_hash_content_of_line_in_context`, this hash is computed.

## Context-free

```
line', col' = remove_whitespace(line content of the REPORT's line, report column)
parts = [ file name, static message, line', str(col'), str(col') ]
```

`remove_whitespace(line, col)`: `prefix = line[:col]`;
`removed = len(prefix) - len(prefix without any whitespace)`;
return (line with **all** whitespace removed, `col - removed`).
Python's whitespace definition (`str.split()`) applies: space, `\t`, `\n`,
`\r`, `\v`, `\f` and Unicode spaces. Columns are 1-based and the slice `line[:col]`
takes the first `col` characters, so the prefix ends **with** the character
at the report column.

Verified example (`core.DivideZero` at `main.c:11:12`, line
`  return a / z;`): prefix `"  return a /"` loses 4 whitespace characters →
col 8; `md5("main.c|||Division by zero|||returna/z;|||8|||8")` =
`d11a6fc279b8946a4bdc0365ff032ae0`.

## Diagnostic-message

Context-free parts, followed by the `message` of every bug path event in
order.

## Path hash (deduplication, not identification)

Used to drop duplicate reports when reading results and on the server:

```
s = "" ; for each bug path event: s += f"{line}|{column}|{message}|{file.path}"
for each annotation (key, value): s += f"|{key}|{value}"
s += checker name ; s += report hash (if any)
path_hash = md5_hex(s)
```

Note `file.path` here is the (possibly trimmed) path, not the original path.

## Clang Static Analyzer's own hash

By default Clang SA reports keep the hash Clang wrote into the plist
(`issue_hash_content_of_line_in_context`). Clang computes it from the
checker name, the enclosing declaration's signature, the normalized source
line and the column. A port that runs Clang SA gets this value from Clang;
nothing to reimplement.
