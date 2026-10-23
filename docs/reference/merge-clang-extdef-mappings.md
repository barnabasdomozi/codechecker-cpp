# Reference: `merge-clang-extdef-mappings`

Usage: [../usage/merge-clang-extdef-mappings.md](../usage/merge-clang-extdef-mappings.md)

Python source:
`analyzer/tools/merge_clang_extdef_mappings/codechecker_merge_clang_extdef_mappings/`
(`cli.py`, `merge_clang_extdef_mappings.py`).

```
merge-clang-extdef-mappings -i INPUT_DIR -o OUTPUT_FILE
```

Both options are required (argparse errors exit 2 here; this tool does not
use CodeChecker's exit-1 parser).

## Algorithm

1. Inputs: every entry matched by `INPUT_DIR/*` (glob order, i.e. directory
   order; not sorted). Each is read as UTF-8 text (undecodable bytes dropped),
   line by line.
2. Each line is stripped, then parsed:
   - **new format** (first character is a digit): `<len>:<rest>`; the name
     is the first `len(len-text) + 1 + len` characters of the line (i.e.
     `"<len>:" + USR`), the file is everything after the following single
     space;
   - **old format**: split at the first space into name and file.
   An empty line makes the tool crash (index error) — inputs never contain
   empty lines in practice.
3. Build `name → set of files`.
4. Keep only names with **exactly one** distinct file (names defined in
   several files are dropped, because CTU could not choose).
5. Write `OUTPUT_FILE` (overwrite): one line per kept name,
   `"<name> <file>\n"`, in first-seen order of the names.

No output on success; exit code 0.

Example (verified): inputs

```
9:c:@F@main /p/main.c
13:c:@F@get_zero /p/lib/util.c
```

and a second file with `13:c:@F@get_zero /p/lib/util_copy.c` → the output
contains only `9:c:@F@main /p/main.c`.
