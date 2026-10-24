# Reference: `tu_collector`

Usage: [../usage/tu-collector.md](../usage/tu-collector.md)

Python source: `tools/tu_collector/tu_collector/tu_collector.py`.

The same functions are used inside CodeChecker: `zip_tu_files` builds the
failure/reproducer zips of `analyze`
([analysis-debugging.md](analysis-debugging.md)), and
`get_dependent_sources` ("dependents") finds the sources that include a
header for `analyze --file` ([skip-and-file-filters.md](skip-and-file-filters.md)).

## 1. Command line

```
tu_collector (-b/--build COMMAND | -l/--logfile COMPILATION_DATABASE)
             (-z/--zip ZIP_FILE | -d/--dependents)
             [-f/--filter PATTERN] [--ctu-deps-dir DIR] [-v/--verbose]
```

Plain `argparse` (errors exit 2). Logging to **stderr**, format
`[<LEVEL>] - <message>`, INFO (`-v` DEBUG). `-f` default `*`.

1. `-l` file missing → ERROR `Compilation database file doesn't exist:
   <path>`, exit 1. Otherwise load it (JSON). Every entry must have a
   `command` string (an `arguments` list is **not** supported:
   `KeyError: 'command'`, exit 1).
2. `-b COMMAND` → a one-entry database `{"file": "", "command": COMMAND,
   "directory": <cwd>}`.
3. `-z`: with `-b`, WARNING `In case of using build command --filter has no
   effect.` (always, because `-f` defaults to `*`) and, if given,
   `In case of using build command --ctu-deps-dir has no effect.`; then
   `zip_tu_files` (§3), INFO `Done.` Despite the warning, an explicit `-f`
   is applied to the one-entry database, whose `file` is `""`, so nothing
   matches and no sources are collected (verified).
4. `-d`: `get_dependent_sources(db, [filter])` (§4); print the result set
   one path per line (unordered), or INFO `No source file dependencies.`

Exit 0 in all other cases, also when dependency collection failed.

## 2. Dependencies of one compile command (`get_dependent_headers`)

1. Split the command with shell rules (`shlex`).
2. Remove options (first occurrence each time, repeatedly, never index 0):
   an argument **starting with** the option string is removed; for options
   "with argument" the next argument is removed too when the option is a
   separate token exactly equal to the option string:
   `-o`(arg), `--output`(arg), `-specs`, `-MM`, `-MF`(arg), `-MP`,
   `-MT`(arg), `-MQ`(arg), `-MD`, `-MMD`, `-MJ`(arg), `-MV`. (Prefix
   matching means e.g. `-MM` also removes `-MMD`, `-o` also removes
   `-ofoo`.)
3. Compiler = `argv[0]`, or `argv[1]` if `argv[0]` ends with `ccache` and
   `argv[1]` is found in `PATH`.
4. Run `<compiler> -E -M -MT __dummy <arguments after the compiler>` (empty
   arguments dropped; arguments starting with `--gcc-toolchain` removed) in
   the entry's `directory`; stdout captured as UTF-8 (errors replaced),
   stderr not captured.
5. Non-zero exit (or failure to start) → error text = the captured stdout
   (or the OS error text); ERROR `Couldn't create dependencies: <text>`.
6. Parse stdout: remove `__dummy: `, remove all `\`, remove all double
   spaces, turn spaces into newlines; each non-empty line is a path,
   joined with the directory (absolute paths stay; not normalised). Paths
   containing spaces are split (a limitation).
7. If an argument `--gcc-toolchain=<dir>` exists: run steps 4–6 again with
   the compiler replaced by `<dir>/bin/g++` (if `argv[0]` contains `++` or
   `cpp`) or `<dir>/bin/gcc`, and add those paths.

Returns the set of paths and the concatenated error texts.

## 3. Zip (`zip_tu_files(zip, db, filter='*', write_mode='w', ctu_deps_dir=None)`)

1. Selected entries = entries whose `file` field (as written in the
   database, often relative) matches `filter` as a shell glob
   (`fnmatch`: case-sensitive on POSIX, `*` matches `/`).
2. `ctu_deps_dir`: for each selected entry compute its action hash: source
   = `normpath(directory/file)`; split the command; remove every argument
   starting with `-o` (and the following argument when it is exactly `-o`);
   `md5_hex(source + "_" + " ".join(args))`. The first file in
   `ctu_deps_dir` whose **name contains** that hash lists one path per line;
   every database entry whose `directory/file` (joined, not normalised) is
   in that list is added to the selection (no duplicates).
3. Collect dependencies (§2) of all selected entries. Errors are gathered
   as `<file>\n<'-' repeated len(file)>\n<error text>\n` per entry.
4. If there are errors: write them to the zip entry `no-sources` (in append
   mode, if the zip already has `no-sources`, use `no-sources_<5 random
   lowercase letters>`). Without errors and in write mode, delete an
   existing zip file first. (With errors in write mode the zip is created
   anew by writing `no-sources`.)
5. Add every dependency file as `sources-root/<path without leading '/'>`
   (normalised), deflate-compressed, skipping names already present.
6. Add `compilation_database.json` = the **whole** input database (not only
   the selection), JSON indented by 2.

## 4. Dependents (`get_dependent_sources(db, header_patterns, jobs=1)`)

1. No patterns → empty set.
2. For every database entry (process pool of `jobs`): dependencies (§2) →
   map dependency path → set of `directory/file` (joined, not normalised)
   of the entries that depend on it. Errors are logged but ignored.
3. For each pattern: `normpath(strip(pattern))` converted from a shell glob
   to an anchored regex (`fnmatch.translate`); every dependency path
   matching it contributes its sources. An empty pattern matches all.
4. Return the union. Since only database entries are collected, headers
   that include the header are not listed.
