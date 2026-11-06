# Known issues in CodeChecker 6.30.0

Bugs and help-text mismatches of the Python reference. For each one, the port
must decide whether to copy the behavior (compatibility) or fix it. The
**Suggestion** line is a recommendation only. When the port fixes an issue, the
decision is written next to the entry and the test is tagged
`[fixed: known-issues #N]` (see [blackbox_tests/](../../blackbox_tests/README.md)).

Issues are sorted by priority, highest first:

| Priority | Meaning | Issues |
|---|---|---|
| **P1** | Wrong results or lost data, with no error. Users act on wrong information without noticing. | [1](#1-cmd-diff-server-run-vs-local-folder-drops-confirmed-reports)–[13](#13-cmd-sum-counts-do-not-add-up-and-contain-placeholder-rows) |
| **P2** | A documented feature does not work: crash, traceback or unusable output. Users notice and are blocked. | [14](#14-server---db-status---db-upgrade-schema---db-force-upgrade-are-ignored-a-server-starts)–[33](#33-cmd-diff--o-table-column-headers-do-not-match-the-data) |
| **P3** | Wrong exit code, misleading message or wrong metadata. Scripts and CI misbehave; the data is right. | [34](#34-cmd-diff-folder-vs-baseline---new-finds-nothing-but-exits-2)–[49](#49-cmd-del--n-takes-wildcards) |
| **P4** | Help text that does not match the behavior. | [50](#50-analyze--e-wname-from-the-help-text-is-rejected)–[59](#59-analyzers---all-and---details-do-nothing) |

Within a priority, issues are sorted by how many users they are likely to hit.

## How the reproductions were verified

Every reproduction below was run against the reference on 2026-11-06:
CodeChecker 6.30.0 (git `451b0c5c`) from its `venv_dev`, Clang/Clang-Tidy
19.1.7, GCC 14.2.0, Cppcheck 2.17.1. Outputs are shortened (`...`), paths are
written as `~/...`. A plain `analyze` runs every analyzer it finds, so with
other analyzers installed the report lists differ, but the issues do not.

Parts marked **(source only)** could not be reproduced with a reasonable
setup and are described from the Python source code.

## Shared setup

Most reproductions start from this setup. Steps that change the server use
their own run names, so they do not disturb each other.

**Reference build** (run in every shell):

```sh
source <codechecker-repo>/venv_dev/bin/activate
export PATH=<codechecker-repo>/build/CodeChecker/bin:/usr/lib/llvm-19/bin:$PATH
```

**Report folders**: the two versions of the [demo project](demo-project.md),
analyzed:

```sh
cp -r blackbox_tests/analyze-targets/demo/v1 ~/demo
cp -r blackbox_tests/analyze-targets/demo/v2 ~/demo2
for d in ~/demo ~/demo2; do
  (cd $d && CodeChecker log -b make -o compile_commands.json \
         && CodeChecker analyze compile_commands.json -o reports)
done
```

**Server**: a server on port 8001 with both versions stored:

```sh
CodeChecker server -w ~/ws -v 8001 &
CodeChecker store ~/demo/reports  -n demo  --url localhost:8001/Default
CodeChecker store ~/demo2/reports -n demo2 --url localhost:8001/Default
```

**Server with authentication** (only for the issues that say so): a second
workspace on port 8002. Start the server once to create `server_config.json`,
stop it, set the `authentication` section as below, then start it again:

```json
"authentication": {
  "enabled": true,
  "super_user": "admin",
  "method_dictionary": {
    "enabled": true,
    "auths": ["admin:admin123", "bob:bob123", "dave:dave123"]
  },
  ...
}
```

```sh
chmod 600 ~/ws-auth/server_config.json
CodeChecker server -w ~/ws-auth -v 8002 &
```

Product permissions are granted in the web UI (product list → *Edit* →
*Permissions*).

---

## P1: Wrong results or lost data

### 1. `cmd diff` (server run vs. local folder) drops `confirmed` reports

[cmd-diff.md](cmd-diff.md)

When one side is a server run and the other a local folder, local reports
whose review status is `confirmed` are treated like suppressed reports and
left out of `--new` and `--unresolved`. A CI gate built on `cmd diff` then
misses exactly the bugs that someone already confirmed. The cause: a review
status object is compared with strings. Between two local folders the
report is listed correctly.

1. Mark the uninitialized-variable report of `~/demo2` as confirmed with a
   rules file:

   ```sh
   cd ~/demo2
   cat > rs.yaml <<'EOF'
   $version: 1
   rules:
     - filters:
         checker_name: clang-diagnostic-uninitialized
       actions:
         review_status: confirmed
   EOF
   CodeChecker analyze compile_commands.json -o reports-rs --review-status-config rs.yaml
   ```

2. Without the rule, the report is unresolved:

   ```sh
   CodeChecker cmd diff -b demo -n reports --unresolved --url localhost:8001/Default
   ```

   ```
   [HIGH] ~/demo2/main.c:25:10: variable 'x' is uninitialized when used here [clang-diagnostic-uninitialized]
   ...
   ```

3. With the rule, it is gone:

   ```sh
   CodeChecker cmd diff -b demo -n reports-rs --unresolved --url localhost:8001/Default
   ```

   The output has `main.c:11:12` (two reports) and `main.c:25:10`
   `cppcheck-uninitvar`, but no `clang-diagnostic-uninitialized`.

4. The same comparison between two local folders lists it:

   ```sh
   CodeChecker cmd diff -b ~/demo/reports -n reports-rs --unresolved
   ```

   ```
   [HIGH] ~/demo2/main.c:25:10: variable 'x' is uninitialized when used here [clang-diagnostic-uninitialized] [Confirmed]
   ```

**Suggestion:** fix. Only leave out reports whose status is not in
`--review-status`.

### 2. `store` without `-f` resolves reports of files that were not analyzed

[store.md](store.md)

The help text of `-f` says: *"By default, CodeChecker would keep reports that
were coming from files not affected by the analysis, and only incrementally
update defect reports for source files that were analysed."* In fact, every
old report that is not in the upload becomes `resolved` (or `off` /
`unavailable`), even if its file was not analyzed at all. Storing a partial
analysis into a full run silently closes the reports of every other file.

1. Store the full analysis, then analyze only `main.c`:

   ```sh
   cd ~/demo
   CodeChecker store reports -n incremental --url localhost:8001/Default
   CodeChecker analyze compile_commands.json -o partial --file "*/main.c"
   ```

2. Store the partial result into the same run, without `-f`:

   ```sh
   CodeChecker store partial -n incremental --url localhost:8001/Default
   ```

3. List the reports with every detection status:

   ```sh
   CodeChecker cmd results incremental --url localhost:8001/Default \
     --detection-status new reopened unresolved resolved off unavailable
   ```

   The `main.c` reports are `unresolved`; `style.cpp:11 cppcheck-zerodiv` is
   `resolved`, although `style.cpp` was not analyzed.

**Suggestion:** decide. Either keep the reports of files that are not in the
analysis (what the help text promises), or fix the help text.

### 3. `cmd suppress` ignores `-f` and always overwrites review statuses

[cmd-suppress.md](cmd-suppress.md)

The help text and the usage page say that without `-f`, reports that already
have a review status keep it. `-f` is ignored: every import overwrites the
existing statuses.

1. Write two suppress files for the same report (the null dereference of
   `demo2`):

   ```sh
   echo '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||first||false_positive' > s1.txt
   echo '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||second||confirmed' > s2.txt
   ```

2. Import the first one. The report becomes a false positive (comment
   `first`):

   ```sh
   CodeChecker cmd suppress demo2 -i s1.txt --url localhost:8001/Default
   ```

3. Import the second one **without** `-f`, and look at the report:

   ```sh
   CodeChecker cmd suppress demo2 -i s2.txt --url localhost:8001/Default
   CodeChecker cmd results demo2 --checker-name core.NullDereference --url localhost:8001/Default \
     --review-status unreviewed confirmed false_positive intentional
   ```

   The status is now `confirmed` with comment `second`. Expected: it stays a
   false positive.

**Suggestion:** fix. Honour `-f`.

### 4. `cmd diff` on two local folders ignores most filters

[local-diff.md](local-diff.md)

With two local sides, only `--severity`, `--checker-name`, `--checker-msg`,
`--file` and `--review-status` work. These filters are accepted and silently
ignored: `--report-hash`, `--analyzer-name`, `--bug-path-length`,
`--filter-preset`, `--detection-status`, `--report-status`, `--tag`,
`--uniqueing` and the date filters (`--detected-before`, ...). The output is
the unfiltered list, so a CI gate counts reports the user meant to exclude.

The help text says `--review-status`, `--report-status`,
`--detection-status`, `--tag` and `--component` *"can be used only if
basename or newname is a run name"*. In fact `--review-status` works for
local folders, the others are accepted (or crash, see
[#23](#23-cmd-diff-crashes-with--c-without--e-and-with---component-on-local-folders)),
and the other filters in the list above are not mentioned at all.

1. Count the new reports:

   ```sh
   cd ~
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new
   ```

2. Add any of the ignored filters. The same reports are printed:

   ```sh
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new --report-hash 0000
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new --analyzer-name nothing
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new --detection-status resolved
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new --detected-before 2000:01:01:00:00:00
   ```

**Suggestion:** fix. Apply the filters, or reject them with an error for local
sides.

### 5. `gerrit_changed_files_to_skipfile.py`: multi-line JSON skips every file

[gerrit-integration.md](gerrit-integration.md)

The script reads the changed-files JSON line by line and only parses lines
that look like a complete object. With pretty-printed JSON no line matches,
so the skipfile only has `-*`. Passed to `analyze -i`, it skips every file:
the analysis does nothing and finds nothing, with no warning. If a line
starts with `{` but the object continues on the next line (like the
two-line example on the usage page), the script crashes instead.

1. A one-line JSON works:

   ```sh
   echo '{"/COMMIT_MSG": {"status": "A"}, "main.c": {"lines_inserted": 1}}' > oneline.json
   gerrit_changed_files_to_skipfile.py oneline.json skipfile
   cat skipfile
   ```

   ```
   +*/main.c
   -*
   ```

2. The same JSON pretty-printed:

   ```sh
   python3 -m json.tool oneline.json > pretty.json
   gerrit_changed_files_to_skipfile.py pretty.json skipfile
   cat skipfile
   ```

   ```
   -*
   ```

   (exit code 0).

3. The two-line example of the usage page:

   ```sh
   printf '%s\n' '{"/COMMIT_MSG": {"status": "A", "lines_inserted": 7, "size_delta": 551, "size": 551},' \
     ' "main.c": {"lines_inserted": 5, "lines_deleted": 1, "size_delta": 10, "size": 700}}' > docs.json
   gerrit_changed_files_to_skipfile.py docs.json skipfile
   ```

   ```
   json.decoder.JSONDecodeError: Expecting property name enclosed in double quotes: line 2 column 1 (char 86)
   ```

**Suggestion:** fix. Parse the whole file (after skipping Gerrit's `)]}'`
prefix line).

### 6. `--single-origin-report` with `--component` returns nothing

[cmd-results.md](cmd-results.md)

`--single-origin-report` should keep the reports whose whole bug path is
inside the component. Instead, a report is excluded as soon as the component
matches **any other file** in the product. That includes the same path
stored by another run. So with a two-file component, or with two runs
of the same project, the result is always empty. The cause: the "exclude"
sub-queries join on `file.id != report.file_id` while filtering files to the
component.

1. Create a component for `main.c` only:

   ```sh
   printf '+*/main.c\n' > main_only.txt
   CodeChecker cmd components add -i main_only.txt main_only --url localhost:8001/Default
   ```

2. The component has five reports in `demo`:

   ```sh
   CodeChecker cmd results demo --component main_only --url localhost:8001/Default
   ```

3. All five are entirely inside `main.c`, but `--single-origin-report` returns
   none, because `demo2`'s `main.c` also matches the component:

   ```sh
   CodeChecker cmd results demo --component main_only --single-origin-report --url localhost:8001/Default
   ```

   In a product that holds only the `demo` run, step 3 returns the five
   reports. With a component of two files (`+*/main.c` and `+*/style.cpp`)
   it returns nothing even then.

**Suggestion:** fix. Exclude only reports that have a bug path point in a
file **outside** the component.

### 7. `store` with several report folders: `off` is computed, and "disabled anywhere" wins

[store.md](store.md)

The help text says: *"If multiple report directories are given, OFF and
UNAVAILABLE detection statuses will not be available."* They are computed.
And a checker that is disabled in one folder and enabled in another counts
as disabled: its old reports become `off` instead of `resolved`. (If the
other folder has a report of that checker, it counts as enabled.)

1. Analyze the demo project with `core.DivideZero` disabled, and a second
   project with all default checkers (no division-by-zero report):

   ```sh
   cd ~/demo && CodeChecker analyze compile_commands.json -o reports-nodiv -d core.DivideZero
   mkdir ~/other && cd ~/other
   printf 'int g(void){int unused=1;return 0;}\n' > other.c
   CodeChecker check -b "gcc -c other.c -o other.o" -o reports --analyzers clangsa
   ```

2. Store the full demo results, then both folders together into the same run:

   ```sh
   cd ~
   CodeChecker store demo/reports -n multi --url localhost:8001/Default
   CodeChecker store demo/reports-nodiv other/reports -n multi --url localhost:8001/Default
   ```

3. The old `main.c:11 core.DivideZero` report is `off`:

   ```sh
   CodeChecker cmd results multi --detection-status off --url localhost:8001/Default
   ```

**Suggestion:** fix the help text; treat a checker that is enabled in any
folder as enabled.

### 8. Makefile mode: wrong checker names and severities, `parse --status` wrong

[makefile-mode.md](makefile-mode.md)

The Makefile from `analyze --makefile` gives different results than
`analyze`:
- clang-tidy result files get other names, so `parse --status` lists
  clang-tidy under "Missing analysis results".
- Cppcheck's raw plist files stay in `reports/cppcheck/` and are not
  post-processed. `parse` reads them with checker names without the
  `cppcheck-` prefix (`zerodiv` instead of `cppcheck-zerodiv`) and with
  severity `UNSPECIFIED` instead of `HIGH`. `parse --status` lists Cppcheck
  as missing too.

1. Generate and run the Makefile:

   ```sh
   cd ~/demo
   CodeChecker analyze compile_commands.json -o mk --makefile
   make -f mk/Makefile -j2
   ```

2. Parse the results:

   ```sh
   CodeChecker parse mk
   ```

   ```
   [HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]
   [UNSPECIFIED] ~/demo/style.cpp:11:12: Division by zero. [zerodiv]
   [UNSPECIFIED] ~/demo/main.c:11:12: Division by zero. [zerodiv]
   [UNSPECIFIED] ~/demo/main.c:25:10: Uninitialized variable: x [uninitvar]
   ...
   ```

   `analyze` gives `[HIGH] ... [cppcheck-zerodiv]` for the same reports.

3. Check the status:

   ```sh
   CodeChecker parse mk --status
   ```

   ```
   Up-to-date analysis results
     clangsa: 3
   ...
   Missing analysis results
     clang-tidy: 3
     cppcheck: 3
   ...
   ```

**Suggestion:** fix. Use the same file names and post-processing as
`analyze`.

### 9. `server -r` (reload) has no effect

[server.md](../reference/server.md)

The reload (`SIGHUP`) is applied only in the main process. The API worker
processes keep the configuration they had at start, so changed settings are
not applied, but the log says `Server configuration reload: Done.`

1. Set `"max_run_count": 1` in `~/ws/server_config.json` (the product
   already has two runs), and reload:

   ```sh
   CodeChecker server -w ~/ws -v 8001 -r
   ```

   The server log shows `Received signal to reload server configuration ...`
   and `Server configuration reload: Done.`

2. Store a new run. It succeeds:

   ```sh
   CodeChecker store ~/demo/reports -n extra --url localhost:8001/Default
   ```

3. Restart the server and store another run. Now it is rejected:

   ```
   [ERROR] - You reached the maximum number of allowed runs (3/1)! Please remove at least 3 run(s) before you try again!
   ```

(Reset `max_run_count` to `null` afterwards.)

**Suggestion:** fix. Apply the reload in the processes that serve requests.

### 10. Two products can share one SQLite database

[cmd-products.md](../reference/cmd-products.md)

The "Database is already in use by another product!" check never matches
SQLite products. SQLite databases must be given as a bare file name, and the
check compares that with the stored absolute path. So a new product can point
to the database of an existing product, and both then show the same runs. For
`config.sqlite` (the server's own database), the result is an internal
server error instead of the intended message.

1. Add a product on top of `Default`'s database:

   ```sh
   CodeChecker cmd products add twin --name twin --sqlite Default.sqlite --url localhost:8001
   ```

   ```
   [INFO] - Product added successfully.
   ```

2. It shows `Default`'s runs:

   ```sh
   CodeChecker cmd runs --url localhost:8001/twin
   ```

   (`demo`, `demo2`, ...)

3. With the config database:

   ```sh
   CodeChecker cmd products add twin2 --name twin2 --sqlite config.sqlite --url localhost:8001
   ```

   ```
   [ERROR] - Internal server error: Internal error
   ```

   (server log: `sqlite3.OperationalError: no such table: runs`).

(`cmd products del twin` removes the product but not the database.)

**Suggestion:** fix. Resolve the file name against the workspace before
comparing.

### 11. `cmd suppress`: false positive / intentional does not close the report

[cmd-suppress.md](../reference/cmd-suppress.md)

Setting a report to false positive or intentional should set its "fixed at"
date, as for source-code comments. The server compares an integer status
with strings, so the date is never set. `--report-status` still shows the
report as closed (it uses the review status), but filters that use the date
get it wrong: `--open-reports-date` returns the report as open, and
`--fixed-after` leaves it out. The same goes for server-side `cmd diff` and
the statistics over time.

1. Mark the null dereference of `demo2` as a false positive:

   ```sh
   echo '19a15bf46b7d3af4b189b3b4f2c28a5b||main.c||not a bug||false_positive' > fp.txt
   CodeChecker cmd suppress demo2 -i fp.txt --url localhost:8001/Default
   ```

2. Ask for reports that are still open in one hour:

   ```sh
   CodeChecker cmd results demo2 --review-status false_positive \
     --open-reports-date $(date -d '+1 hour' +%Y:%m:%d:%H:%M:%S) --url localhost:8001/Default
   ```

   Only `core.NullDereference` (line 21) is returned. The false positives
   from source-code comments are correctly closed.

3. Ask for reports fixed after 2000:

   ```sh
   CodeChecker cmd results demo2 --review-status false_positive \
     --fixed-after 2000:01:01:00:00:00 --url localhost:8001/Default
   ```

   The source-code-comment false positives are returned; the null dereference
   is not. `-o json` shows `"fixedAt": null` for it.

**Suggestion:** fix the comparison.

### 12. Cppcheck reports on non-ASCII paths point to a file that does not exist

[analyze.md](analyze.md)

Cppcheck 2.17.1 writes each byte of a non-ASCII character as `x` in its plist
output (`ünïcødé/naïve.cpp` → `xxnxxcxxdxx/naxxve.cpp`). CodeChecker takes
the path over, so the report points to a missing file (see also
[blackbox_tests/analyze-targets/odd-paths](../../blackbox_tests/analyze-targets/odd-paths/README.md)).

1. Analyze a file in a non-ASCII folder:

   ```sh
   mkdir -p ~/odd/ünïcødé && cd ~/odd
   printf 'int f(int a){int z=0;return a/z;}\n' > ünïcødé/naïve.cpp
   CodeChecker log -b "g++ -c ünïcødé/naïve.cpp -o naive.o" -o compile_commands.json
   CodeChecker analyze compile_commands.json -o reports --analyzers cppcheck clangsa
   ```

2. Parse:

   ```sh
   CodeChecker parse reports
   ```

   ```
   [HIGH] ~/odd/ünïcødé/naïve.cpp:1:30: Division by zero [core.DivideZero]
   [HIGH] ~/odd/xxnxxcxxdxx/naxxve.cpp:1:30: Division by zero. [cppcheck-zerodiv]
   WARNING: source file content is changed or missing. Please re-analyze your project to update the reports.
   ```

**Suggestion:** map the path back to the analyzed source file.

### 13. `cmd sum`: counts do not add up, and contain placeholder rows

[cmd-sum.md](cmd-sum.md)

The "All reports" column leaves out false positives that the "False
positive" column counts. The long checker table also has `__FAKE__` and
`NOT FOUND` rows.

1. ```sh
   CodeChecker cmd sum -n demo --url localhost:8001/Default
   ```

   ```
   │ Checker          │ Severity │ Unreviewed │ Confirmed │ False positive │ Intentional │ All reports │
   │ core.DivideZero  │ HIGH     │ 1          │ 0         │ 2              │ 0           │ 1           │
   │ cppcheck-zerodiv │ HIGH     │ 2          │ 0         │ 1              │ 0           │ 2           │
   ...
   │ Total            │ -        │ 6          │ 0         │ 3              │ 0           │ 6           │
   ...
   │ Checker Name │ guideline │ Severity    │ Enabled in all selected runs │ Closed Reports │ Outstanding Reports │
   │ __FAKE__     │           │ UNSPECIFIED │ True                         │ 0              │ 0                   │
   │ NOT FOUND    │           │ UNSPECIFIED │ True                         │ 0              │ 0                   │
   ...
   ```

**Suggestion:** decide on the counting; at least drop the placeholder rows.

---

## P2: A documented feature does not work

### 14. `server --db-status`, `--db-upgrade-schema`, `--db-force-upgrade` are ignored; a server starts

[server-database.md](server-database.md)

The options should print the database status, or upgrade the databases,
and exit. They are ignored and a normal server starts. The cause:
`server_init_start()` passes `context.migration_root`, which does not exist
(renamed to `run_migration_root`). The `AttributeError` is swallowed by an
`except AttributeError` meant for a missing `args.status`.

1. ```sh
   CodeChecker server -w ~/ws -v 8002 --db-status all
   ```

   ```
   [INFO] - Status of products:
   ...
   [INFO] - Server waiting for client requests on [localhost:8002]
   ```

   The server keeps running until it is stopped. `--db-upgrade-schema all`,
   `--db-upgrade-schema Default` and `--db-force-upgrade` do the same.

Related, for a port that implements the intended behavior:
- **(source only)** The intended `--db-upgrade-schema P` code checks that `P`
  exists, then upgrades **all** products. Upgrade only `P`.
- The help text of `--db-force-upgrade` ("database migration without user
  interaction") suggests it goes with `--db-upgrade-schema`, but the two
  options are mutually exclusive:

  ```sh
  CodeChecker server -w ~/ws --db-upgrade-schema all --db-force-upgrade
  ```

  ```
  CodeChecker server: error: argument --db-force-upgrade: not allowed with argument --db-upgrade-schema
  ```

**Suggestion:** fix. Print the status or upgrade, then exit with the status
code.

### 15. `fixit` crashes on `cmd diff -o json` input

[fixit.md](fixit.md)

The documented pipe `cmd diff ... -o json | fixit` does not work.
`cmd diff -o json` writes the local report format (no `checkedFile`), so
`fixit` crashes whenever the diff has reports. An empty diff disables the
filtering entirely, and every fix is listed. Only `cmd results -o json`
works.

1. Analyze both versions with a fix-it checker:

   ```sh
   for d in ~/demo ~/demo2; do
     (cd $d && CodeChecker analyze compile_commands.json -o tidy --analyzers clang-tidy -e modernize-use-nullptr)
   done
   cd ~
   ```

2. Pipe a non-empty diff (the new `modernize-use-nullptr` report of
   `demo2`):

   ```sh
   CodeChecker cmd diff -b demo/tidy -n demo2/tidy --new -o json | CodeChecker fixit demo2/tidy
   ```

   ```
       diag_msg['FilePath'] == report['checkedFile']
   KeyError: 'checkedFile'
   ```

   (exit code 1).

3. Pipe an empty diff. Every fix is listed, although nothing was selected:

   ```sh
   CodeChecker cmd diff -b demo/tidy -n demo/tidy --new -o json | CodeChecker fixit demo/tidy
   ```

   ```
     DiagnosticName: modernize-use-nullptr
   ...
   ```

**Suggestion:** fix. Accept both JSON report formats; treat an empty list as
"nothing to fix".

### 16. `cmd export` → `cmd import` fails for whole-second timestamps

[cmd-export-import.md](cmd-export-import.md)

`cmd export` writes dates as `str(datetime)`, which has no fraction when the
microseconds are zero (`2026-01-02 03:04:05`). `cmd import` requires
`%Y-%m-%d %H:%M:%S.%f` for comment dates and review status dates, so the
whole import fails.

1. Import a comment whose time has zero microseconds:

   ```sh
   cat > in.json <<'EOF'
   {"comments": {"249a121710b6a5cb467697f323de0d5f": [{"id": 1, "author": "alice",
     "message": "whole second", "createdAt": "2026-01-02 03:04:05.000000", "kind": 0}]},
    "reviewData": {}}
   EOF
   CodeChecker cmd import -i in.json --url localhost:8001/Default
   ```

   ```
   [INFO] - Import successful!
   ```

2. Export it. The date has no fraction:

   ```sh
   CodeChecker cmd export -n demo --url localhost:8001/Default > exported.json
   ```

   (`"createdAt": "2026-01-02 03:04:05"`)

3. Import the export into another product:

   ```sh
   CodeChecker cmd products add copy --name copy --url localhost:8001
   CodeChecker cmd import -i exported.json --url localhost:8001/copy
   ```

   ```
   [ERROR] - time data '2026-01-02 03:04:05' does not match format '%Y-%m-%d %H:%M:%S.%f'
   ```

   (exit code 1). A review status `date` without fraction fails the same way.

**Suggestion:** fix. Accept both forms.

### 17. `review_status.yaml`: a list as a filter value crashes `analyze`, `parse` and `cmd diff`

[review-status-and-suppression.md](review-status-and-suppression.md)

A list for `report_hash` or `filepath` (e.g. `report_hash: [abc]`) crashes
with `TypeError`. `analyze --review-status-config` counts every file that
has reports as "Failed to analyze" (exit code 3). A list for `checker_name`
does not crash, but never matches.

1. Write a rules file with a list:

   ```sh
   cd ~/demo
   cat > rs-list.yaml <<'EOF'
   $version: 1
   rules:
     - filters:
         report_hash: [249a1217]
       actions:
         review_status: intentional
   EOF
   ```

2. Analyze with it:

   ```sh
   CodeChecker analyze compile_commands.json -o rs-an --analyzers clangsa --review-status-config rs-list.yaml
   ```

   ```
   [INFO] - [1/3] clangsa analyzed util.c successfully.
   ...
   [INFO] - Failed to analyze
   [INFO] -   clangsa: 2
   Traceback (most recent call last):
   ...
   ```

   (exit code 3).

3. Parse (`parse` reads the copy of the rules file in the report folder):

   ```sh
   CodeChecker parse rs-an
   ```

   ```
   TypeError: startswith first arg must be str or a tuple of str, not list
   ```

   (exit code 1). `filepath: ["*/main.c"]` gives `TypeError: expected str,
   bytes or os.PathLike object, not list`. `cmd diff` with such a folder
   crashes the same way.

4. With `checker_name: [clang-diagnostic-unused-variable]`, `parse` shows the
   unused-variable report as unreviewed.

**Suggestion:** fix. Validate the value types when loading the file.

### 18. `store` with invalid source-code comments commits the run, then fails

[store.md](../reference/store.md)

When a source file has invalid review status comments (e.g. two comments for
one report), the server commits the run and then fails the task. Building the
error table crashes, so the user gets an unrelated message instead of the
list of bad comments. `store` exits 1 although the run is stored.

1. ```sh
   mkdir ~/badc && cd ~/badc
   cat > bad.c <<'EOF'
   int f(int a) {
     int z = 0;
     // codechecker_false_positive [core.DivideZero] one
     // codechecker_confirmed [core.DivideZero] two
     return a / z;
   }
   EOF
   CodeChecker check -b "gcc -c bad.c -o bad.o" -o reports --analyzers clangsa
   ```

   (`parse` already says `Multiple source code comments can be found for
   'core.DivideZero' checker in '~/badc/bad.c' at line 5.`)

2. ```sh
   CodeChecker store reports -n badc --url localhost:8001/Default
   ```

   ```
   [ERROR] - Storing the reports failed! The job terminated in status 'FAILED'. The comments associated with the failure are:
   Row has incorrect number of values, (actual) 1!=3 (expected)
   ```

   (exit code 1). `cmd runs` lists `badc` anyway.

**Suggestion:** fix. Report the comment errors clearly, and decide whether to
store or reject before committing.

### 19. `cmd token new` without `--expiration` always fails

[server-authentication.md](server-authentication.md)

Needs the **server with authentication**.

1. ```sh
   CC_PASSWORD=admin123 CodeChecker cmd login admin --url localhost:8002
   CodeChecker cmd token new ci --url localhost:8002
   ```

   ```
   [ERROR] - Invalid expiration length. The length must be between 1 and 365!
   ```

   (exit code 1).

2. With `--expiration 30` it works: `The token is valid until: ...`

**Suggestion:** fix. Default to the server's maximum
(`max_pers_auth_token_expiration_length`).

### 20. Password file: a `:` in a saved password crashes every command

[server-api.md](../reference/server-api.md)

Needs the **server with authentication**, with
`"carol:pa:ss"` added to `auths` (restart the server).

- A saved password that contains `:` crashes every client command that loads
  the password file, with `ValueError` (the user/password split expects
  exactly one `:`).
- `CC_PASSWORD` with a `:` is cut at its second `:`, so the login is
  refused. Typing the password at the prompt works.
- A password file that is not valid JSON, or has no `credentials` key,
  crashes with `KeyError: 'credentials'`.

1. Save carol's password:

   ```sh
   echo '{"client_autologin": true, "credentials": {"localhost:8002": "carol:pa:ss"}}' > ~/.codechecker.passwords.json
   chmod 600 ~/.codechecker.passwords.json
   CodeChecker cmd runs --url localhost:8002/Default
   ```

   ```
       user, _ = v.split(":")
   ValueError: too many values to unpack (expected 2)
   ```

2. Without the file, with `CC_PASSWORD`:

   ```sh
   rm ~/.codechecker.passwords.json
   CC_PASSWORD='pa:ss' CodeChecker cmd login carol --url localhost:8002
   ```

   ```
   [ERROR] - Authentication denied
    Invalid credentials supplied for user 'carol'. Refusing authentication!
   ```

3. At the prompt it works:

   ```sh
   echo 'pa:ss' | CodeChecker cmd login carol --url localhost:8002
   ```

   ```
   [INFO] - Server reported successful authentication.
   ```

4. A broken file:

   ```sh
   echo 'not json' > ~/.codechecker.passwords.json
   CodeChecker cmd runs --url localhost:8002/Default
   ```

   ```
   KeyError: 'credentials'
   ```

**Suggestion:** fix. Split at the first `:` only; give a clear error for a
broken file.

### 21. `CC_ANALYZER_BIN` with a missing file crashes every analyzer command

[analyzers.md](analyzers.md)

A path to a file that does not exist crashes every command that looks up
analyzers (`analyzers`, `checkers`, `analyze`, `check`).

1. ```sh
   CC_ANALYZER_BIN=cppcheck:/no/such/cppcheck CodeChecker analyzers
   ```

   ```
   TypeError: stat: path should be string, bytes, os.PathLike or integer, not NoneType
   ```

   (exit code 1). `checkers` and `analyze` crash the same way.

**Suggestion:** fix. Report the binary as not found, like any missing
analyzer.

### 22. An unknown `--severity`, `--review-status` or `--detection-status` value crashes

[cmd-results.md](cmd-results.md)

The intended `Invalid ... value(s)` message is unreachable. Both
`cmd results` and `cmd diff` crash with `KeyError`.

1. ```sh
   CodeChecker cmd results demo --review-status suppress --url localhost:8001/Default
   ```

   ```
   KeyError: 'SUPPRESS'
   ```

   (exit code 1). `--severity extreme` gives `KeyError: 'EXTREME'`,
   `--detection-status gone` gives `KeyError: 'GONE'`. `cmd diff -b
   ~/demo/reports -n ~/demo2/reports --new --review-status suppress` crashes
   the same way.

**Suggestion:** fix. Print the error and exit 1.

### 23. `cmd diff` crashes with `-c` without `-e`, and with `--component` on local folders

[local-diff.md](local-diff.md)

1. ```sh
   cd ~
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new -c
   ```

   ```
   TypeError: stat: path should be string, bytes, os.PathLike or integer, not NoneType
   ```

2. ```sh
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new --component x
   ```

   ```
   AttributeError: 'NoneType' object has no attribute 'getSourceComponents'
   ```

Both exit with code 1.

**Suggestion:** fix. `-c` without `-e` is an argument error; `--component`
with two local sides should be rejected (see also
[#4](#4-cmd-diff-on-two-local-folders-ignores-most-filters)).

### 24. `report-converter -t clang-tidy`: a blank line aborts the conversion

[report-converter.md](report-converter.md)

A blank line after a diagnostic's source-code snippet (after the `^` line or
a fix-it line) aborts the whole conversion. A blank line between diagnostics
without snippets is fine.

1. ```sh
   mkdir ~/rc && cd ~/rc
   printf 'int g(int a){int z=0;return a/z;}\nint *n(){return 0;}\n' > lone.c
   clang-tidy lone.c -checks='-*,modernize-use-nullptr' -- -x c++ > tidy.out
   (cat tidy.out; echo) > tidy-blank.out
   ```

2. ```sh
   report-converter -t clang-tidy -o out tidy.out         # works
   report-converter -t clang-tidy -o out2 tidy-blank.out
   ```

   ```
   ValueError: Unexpected line: 
   . Expected a FixIt line!
   ```

   (exit code 1).

**Suggestion:** fix. Treat a blank line as the end of the diagnostic.

### 25. `report-converter --meta analyzer_command=...` is stored garbled

[report-converter.md](report-converter.md)

`report-converter` writes `command` into `metadata.json` as a string, but the
server joins it as a list. The command is stored with a space between every
character.

1. ```sh
   cd ~/rc
   report-converter -t clang-tidy -o out1 tidy.out --meta analyzer_command="clang-tidy lone.c"
   CodeChecker store out1 -n rcrun --url localhost:8001/Default
   ```

2. Look at the analysis info of `rcrun` in the web UI, or in the database:

   ```sh
   python3 -c "import sqlite3,zlib; print(zlib.decompress(sqlite3.connect('$HOME/ws/Default.sqlite').execute('select analyzer_command from analysis_info order by id desc').fetchone()[0]))"
   ```

   ```
   b'c l a n g - t i d y   l o n e . c'
   ```

**Suggestion:** fix. Write `command` as a list in `metadata.json`.

### 26. `report-converter`: argument errors give tracebacks

[report-converter.md](report-converter.md)

- `--meta` takes any number of values, so it swallows the inputs that follow
  it.
- A `--meta` value without `=` crashes.
- An empty input directory crashes.

```sh
cd ~/rc
report-converter -t clang-tidy -o o2 --meta analyzer_version=19 tidy.out
# report-converter: error: the following arguments are required: input   (exit 2)
report-converter -t clang-tidy -o o3 tidy.out --meta foo
# ValueError: not enough values to unpack (expected 2, got 1)            (exit 1)
mkdir emptyd && report-converter -t clang-tidy -o o4 emptyd
# AssertionError                                                         (exit 1)
```

**Suggestion:** fix. Give clear errors.

### 27. `store`: an invalid report annotation fails with an unrelated message

[store.md](../reference/store.md)

An unknown annotation key, or a badly formatted value, should fail with
`'<key>' is not an allowed report annotation.` or `'<value>' has wrong
format. ...`. The code uses an error code that no longer exists.

1. Add an unknown annotation to a result file:

   ```sh
   cd ~/rc
   printf 'int g(int a){int z=0;return a/z;}\n' > ann.c
   CodeChecker check -b "gcc -c ann.c -o ann.o" -o ann --analyzers clangsa
   python3 - <<'EOF'
   import glob, plistlib
   f = glob.glob('ann/*.plist')[0]
   d = plistlib.load(open(f, 'rb'))
   d['diagnostics'][0]['report-annotation'] = {'priority': 'high'}
   plistlib.dump(d, open(f, 'wb'))
   EOF
   ```

2. ```sh
   CodeChecker store ann -n ann --url localhost:8001/Default
   ```

   ```
   [ERROR] - Storing the reports failed! The job terminated in status 'FAILED'. The comments associated with the failure are:
   type object 'ErrorCode' has no attribute 'REPORT_FORMAT'
   ```

   (exit code 1). `{'timestamp': 'yesterday'}` gives the same message.

**Suggestion:** fix. Report the intended message.

### 28. `tu_collector -l` crashes on compilation databases that use `arguments`

[tu-collector.md](tu-collector.md)

1. Convert the demo database from `command` to `arguments`:

   ```sh
   cd ~/demo
   python3 - <<'EOF'
   import json
   cc = json.load(open('compile_commands.json'))
   for e in cc:
       e['arguments'] = e.pop('command').split()
   json.dump(cc, open('args_cc.json', 'w'), indent=1)
   EOF
   ```

2. ```sh
   tu_collector -l args_cc.json -z tu.zip
   ```

   ```
   KeyError: 'command'
   ```

   (exit code 1).

**Suggestion:** fix. Support `arguments`.

### 29. `cmd components add` for an existing name crashes without a terminal

[cmd-components.md](cmd-components.md)

Adding a component that exists asks `Do you want to update? Y(es)/n(o)` and
crashes when standard input is closed (for example in CI). There is no
non-interactive way to update a component.

1. ```sh
   printf '+*/main.c\n' > comp.txt
   CodeChecker cmd components add -i comp.txt mycomp --url localhost:8001/Default
   CodeChecker cmd components add -i comp.txt mycomp --url localhost:8001/Default < /dev/null
   ```

   ```
   [INFO] - The source component 'mycomp' already exist!
   Do you want to update? Y(es)/n(o) Traceback (most recent call last):
   ...
   EOFError: EOF when reading a line
   ```

   (exit code 1).

**Suggestion:** fix. Add a `--force` / non-interactive path.

### 30. `store --zip-loc DIR` crashes if `DIR` does not exist

[store.md](store.md)

1. ```sh
   CodeChecker store ~/demo/reports -n demo --zip-loc ~/no/such/dir --url localhost:8001/Default
   ```

   ```
   FileNotFoundError: [Errno 2] No such file or directory: '~/no/such/dir/tmpxh81qcp0-store'
   ```

   (exit code 1).

**Suggestion:** fix. Create the folder or give a clear error.

### 31. `memory_safety_reporter` crashes on a report folder without `metadata.json` or `conf/`

[memory-safety-reporter.md](memory-safety-reporter.md)

1. ```sh
   cp -r ~/demo/reports ~/msr1 && rm ~/msr1/metadata.json
   cp -r ~/demo/reports ~/msr2 && rm -r ~/msr2/conf
   memory_safety_reporter -r ~/msr1 -o ~/msr1.out
   # FileNotFoundError: [Errno 2] No such file or directory: '~/msr1/metadata.json'   (exit 1)
   memory_safety_reporter -r ~/msr2 -o ~/msr2.out
   # FileNotFoundError: [Errno 2] No such file or directory: '~/msr2/conf'            (exit 1)
   ```

**Suggestion:** fix. Give a clear error, or skip the missing part.

### 32. A bad server URL prints a traceback

[server-api.md](../reference/server-api.md)

For the server URL of `cmd login`, `token`, `permissions`, `products` and
`serverside-tasks`, an unknown protocol or a non-numeric port prints a
`ValueError` traceback. A bad product URL gets a clear message and exit
code 2.

1. ```sh
   CodeChecker cmd products list --url ftp://localhost:8001
   # ValueError: 'ftp' is not a protocol understood by CodeChecker   (exit 1)
   CodeChecker cmd products list --url localhost:abc
   # ValueError: invalid literal for int() with base 10: 'abc'       (exit 1)
   CodeChecker cmd runs --url localhost:abc/Default
   # [ERROR] - Malformed product URL was provided. ...                (exit 2)
   ```

**Suggestion:** fix. Print an error message and exit 2, like for product
URLs.

### 33. `cmd diff -o table`: column headers do not match the data

[local-diff.md](local-diff.md)

The header is `File, Checker, Severity, Msg, Source`. The columns hold
severity, location, message, checker and source line. `rows` and `csv` have
the same problem.

1. ```sh
   cd ~
   CodeChecker cmd diff -b demo/reports -n demo2/reports --new -o table
   ```

   ```
   │ File │ Checker                │ Severity                                               │ Msg                  │ Source                │
   │ HIGH │ ~/demo2/style.cpp:4:35 │ Division by zero                                       │ core.DivideZero      │   int *p = 0; ...     │
   │ HIGH │ ~/demo2/main.c:21:18   │ Dereference of null pointer (loaded from variable 'q') │ core.NullDereference │   int *q = 0; *q = 1; │
   ...
   ```

**Suggestion:** fix.

---

## P3: Wrong exit code, misleading message or wrong metadata

### 34. `cmd diff` folder vs. baseline `--new` finds nothing but exits 2

[local-diff.md](local-diff.md)

Exit code 2 means "differences found". With a folder as `-b` and a baseline
file as `-n`, `--new` can never print a report (a baseline has only hashes),
but it exits 2. A CI gate built this way always fails.

1. ```sh
   cd ~
   CodeChecker parse demo/reports -e baseline -o reports.baseline
   CodeChecker cmd diff -b demo2/reports -n reports.baseline --new
   ```

   ```
   [INFO] - Couldn't get local reports for the following baseline report hashes: 04d39a25..., ...
   ...
   │ Number of analyzer reports                │ 0 │
   ```

   (exit code 2).

**Suggestion:** fix. Exit 0 when the difference is empty.

### 35. `fixit` without `clang-apply-replacements` exits 0

[fixit.md](fixit.md)

1. With a `PATH` that has no `clang-apply-replacements` (on Debian, leave out
   `/usr/lib/llvm-19/bin`):

   ```sh
   cd ~/demo
   CodeChecker analyze compile_commands.json -o tidy --analyzers clang-tidy -e modernize-use-nullptr
   CodeChecker fixit tidy -a
   ```

   ```
   [ERROR] - clang-apply-replacements tool is not found
   ```

   Exit code 0, and `style.cpp` is unchanged.

**Suggestion:** fix. Exit non-zero.

### 36. `tu_collector -b "make"` writes an almost empty zip and exits 0

[tu-collector.md](tu-collector.md)

`-b` expects a compiler command: it appends `-M -MT ...` to it to get the
dependencies. With a build-system command this fails, but the tool says
`Done.` and exits 0.

1. ```sh
   cd ~/demo && make clean
   tu_collector -b "make" -z ~/tu.zip
   ```

   ```
   make: invalid option -- 'M'
   ...
   [ERROR] - Couldn't create dependencies: 
   [INFO] - Done.
   ```

   Exit code 0. The zip has only `compilation_database.json`, with the entry
   `{"file": "", "command": "make", ...}`.

**Suggestion:** fix. Exit non-zero.

### 37. `analyze` on a source file without a compilation database exits 0

[analyze.md](analyze.md)

When a source file or folder is given and no `compile_commands.json` is found
in its parent folders, nothing is analyzed, but the exit code is 0.

1. ```sh
   mkdir ~/lone && cd ~/lone
   printf 'int f(int a){int z=0;return a/z;}\n' > lone.c
   CodeChecker analyze lone.c -o reports
   ```

   ```
   [WARNING] - No analysis is required.
   [WARNING] - There were no compilation commands in the provided compilation database or all of them were skipped.
   ```

   (exit code 0).

**Suggestion:** consider exit 1.

### 38. `post-process-stats` with a missing input folder exits 0

[exit-codes.md](../reference/exit-codes.md)

1. ```sh
   post-process-stats -i ~/no/such/dir ~/stats-out
   ```

   ```
   [INFO] - Starting to post-process statistical results...
   [INFO] - Done.
   ```

   (exit code 0).

**Suggestion:** fix. Print an error and exit 1.

### 39. `cmd products del` of a missing product exits 0

[cmd-products.md](../reference/cmd-products.md)

1. ```sh
   CodeChecker cmd products del nosuch --url localhost:8001
   ```

   ```
   [ERROR] - The product 'nosuch' does not exist!
   ```

   (exit code 0).

**Suggestion:** fix. Exit 1.

### 40. `cmd history -n PATTERN` prints every run when nothing matches

[cmd-runs-history.md](cmd-runs-history.md)

A pattern that matches no run gives an empty run list. The empty list is then
sent as "no filter", so the history of all runs is printed.

1. ```sh
   CodeChecker cmd history -n nomatch --url localhost:8001/Default
   ```

   ```
   │ Date                       │ Run name │ ...
   │ 2026-11-06 11:29:35.302867 │ demo2    │ ...
   │ 2026-11-06 11:29:28.954225 │ demo     │ ...
   ```

   (exit code 0).

**(source only)** Only the newest 500 entries (`MAX_QUERY_SIZE`) are
fetched; older history is never shown.

**Suggestion:** fix. Print nothing (or warn) when no run matches; page the
results.

### 41. `cmd serverside-tasks`: some filters are ignored

[cmd-serverside-tasks.md](cmd-serverside-tasks.md)

- `--no-product` and `--no-username` set attributes that are not part of the
  filter sent to the server, so they are ignored.
- `--last-seen-before` / `--last-seen-after` send the values of
  `--started-before` / `--started-after`, so on their own they do nothing.

1. ```sh
   CodeChecker cmd serverside-tasks --no-product --output json --url localhost:8001
   ```

   Returns the store tasks, each with `"productEndpoint": "Default"`.
   Expected: only server-wide tasks (none).

2. ```sh
   CodeChecker cmd serverside-tasks --last-seen-after $(date -d '+1 day' +%Y:%m:%d:%H:%M:%S) \
     --output json --url localhost:8001
   ```

   Returns every task. With `--started-after` and the same date, none.

**Suggestion:** fix.

### 42. The server's start-up check only looks at the first product

[server-database.md](../reference/server-database.md)

If a product database has a problem, the server should print `There are some
database issues.` and ask `Do you want to start the server?`. The loop stops
after the first product, so problems in other products are only shown in the
status table.

1. Create a second product and give its database an unknown schema version:

   ```sh
   CodeChecker cmd products add solo --name solo --url localhost:8001
   # stop the server
   python3 -c "import sqlite3; c=sqlite3.connect('$HOME/ws/solo.sqlite'); c.execute(\"update alembic_version set version_num='0123456789ab'\"); c.commit()"
   CodeChecker server -w ~/ws -v 8001
   ```

   The table shows `solo │ Database schema mismatch! NOT possible to
   upgrade.`, but there is no error and no question; the server starts.

2. Restore `solo` (`feb5f01b5b52`) and break `Default` instead. Now the server
   prints `There are some database issues.` and asks.

**Suggestion:** fix. Check every product.

### 43. Review status rules: author and date are not copied to newly stored reports

[store.md](../reference/store.md)

When a stored report matches a review status rule, the report should get the
rule's author and date. The code assigns misspelled attribute names, so the
report gets the storing user and the store time.

1. Create a rule dated 2026-01-02 with `cmd import`:

   ```sh
   cat > rule.json <<'EOF'
   {"comments": {}, "reviewData": {"249a121710b6a5cb467697f323de0d5f":
     {"status": 3, "comment": "kept on purpose", "author": "alice", "date": "2026-01-02 03:04:05.000001"}}}
   EOF
   CodeChecker cmd import -i rule.json --url localhost:8001/Default
   ```

2. Store a new run with that report, and look at its review data:

   ```sh
   CodeChecker store ~/demo/reports -n after-rule --url localhost:8001/Default
   CodeChecker cmd results after-rule --review-status intentional -o json --url localhost:8001/Default
   ```

   `"reviewData": {"status": 3, "comment": "kept on purpose", "author":
   "Anonymous", "date": "2026-11-06 11:40:59.319145", ...}`: the store time,
   not `2026-01-02 03:04:05.000001`. (The existing run `demo` has the rule's
   date.)

**Suggestion:** fix. Copy them.

### 44. `cmd import`: review status rules get the importing user as author

[cmd-export-import.md](cmd-export-import.md)

In the reproduction of [#43](#43-review-status-rules-author-and-date-are-not-copied-to-newly-stored-reports),
the imported author is `alice`, but `cmd results demo --review-status
intentional -o json` shows `"author": "Anonymous"` (the importing user on a
server without authentication).

**Suggestion:** decide; probably keep the exported author.

### 45. `cmd components add` (update) does not update the stored user name

[cmd-components.md](cmd-components.md)

Needs the **server with authentication**; give `bob` `PRODUCT_ADMIN` on
`Default`. The user name is not shown by the CLI, only in the database.

1. As `admin`, add a component; as `bob`, update it:

   ```sh
   printf '+*/main.c\n' > comp.txt
   CC_PASSWORD=admin123 CodeChecker cmd login admin --url localhost:8002
   CodeChecker cmd components add -i comp.txt comp --url localhost:8002/Default
   CC_PASSWORD=bob123 CodeChecker cmd login bob --url localhost:8002
   echo y | CodeChecker cmd components add -i comp.txt comp --url localhost:8002/Default
   ```

2. ```sh
   python3 -c "import sqlite3; print(list(sqlite3.connect('$HOME/ws-auth/Default.sqlite').execute('select name, username from source_components')))"
   ```

   ```
   [('comp', 'admin')]
   ```

**Suggestion:** fix.

### 46. Unauthenticated access blames the password file

[server-authentication.md](server-authentication.md)

Needs the **server with authentication**, and no
`~/.codechecker.passwords.json`.

1. ```sh
   CodeChecker cmd runs --url localhost:8002/Default
   ```

   ```
   [INFO] - Password file not found.
   [ERROR] - Invalid pre-configured credentials.
   [ERROR] - Your password has been changed or personal access token has been removed which is used by your "~/.codechecker.passwords.json" file. Please remove or change invalid credentials.
   ```

**Suggestion:** fix the message (e.g. "Please log in with `CodeChecker cmd
login`.").

### 47. Server start-up warns about negative worker counts for the default setting

[server.md](server.md)

1. Start a server with the default `server_config.json`:

   ```
   [WARNING] - Number of worker processes can not be negative! Default value will be used: 12
   [WARNING] - Number of task worker processes can not be negative! Default value will be used: 12
   ```

   The setting is `null` (use the default), not negative.

**Suggestion:** fix. Only warn for negative values.

### 48. Task permissions differ from the API docs and the usage page

[cmd-serverside-tasks.md](cmd-serverside-tasks.md)

- Viewing and listing product tasks needs `PRODUCT_ACCESS`; the API docs say
  `PRODUCT_ADMIN`.
- The task's submitter can cancel it; the usage page says only superusers.

Needs the **server with authentication**; give `bob` `PRODUCT_ACCESS` and
`PRODUCT_STORE` on `Default`, and `dave` `PRODUCT_ACCESS`.

1. As `bob`, store a run and list the product's tasks (works with
   `PRODUCT_ACCESS` only):

   ```sh
   CC_PASSWORD=bob123 CodeChecker cmd login bob --url localhost:8002
   CodeChecker store ~/demo/reports -n bobrun --url localhost:8002/Default
   CodeChecker cmd serverside-tasks --product Default --url localhost:8002
   ```

2. `bob` can cancel his own task (no error; it has already finished, so
   nothing changes):

   ```sh
   CodeChecker cmd serverside-tasks -t <TOKEN> --kill --url localhost:8002
   ```

3. `dave` can view it but not cancel it:

   ```sh
   CC_PASSWORD=dave123 CodeChecker cmd login dave --url localhost:8002
   CodeChecker cmd serverside-tasks -t <TOKEN> --url localhost:8002          # works
   CodeChecker cmd serverside-tasks -t <TOKEN> --kill --url localhost:8002
   ```

   ```
   [ERROR] - Unauthorized to access
    The task does not exist, or you are not authorized to cancel the task. (Only the task's submitter, people with PRODUCT_ADMIN on the task's product, or a SUPERUSER can cancel the task.)
   ```

**Suggestion:** decide and document.

### 49. `cmd del -n` takes wildcards

[cmd-run-management.md](../reference/cmd-run-management.md)

The usage page says "no wildcards". Names are case-insensitive patterns with
`*` wildcards. A name without `*` matches only that run (in any case).

1. ```sh
   CodeChecker store ~/demo/reports -n after-x --url localhost:8001/Default
   CodeChecker cmd del -n 'AFTER-*' --url localhost:8001/Default
   ```

   `after-x` is deleted.

**Suggestion:** document it (or match exactly).

---

## P4: Help text that does not match the behavior

### 50. `analyze -e W<name>` from the help text is rejected

[checker-selection.md](checker-selection.md)

The help says compiler warnings *"need to be enabled by their original name,
e.g. '-e Wliteral-conversion'"*. That form is rejected; the working name is
`clang-diagnostic-<name>`.

```sh
cd ~/demo
CodeChecker analyze compile_commands.json -o h1 --analyzers clang-tidy -e Wliteral-conversion
# [ERROR] - No checker(s) with these names was found: Wliteral-conversion.   (exit 1)
CodeChecker analyze compile_commands.json -o h1 --analyzers clang-tidy -e clang-diagnostic-literal-conversion
# works
```

### 51. `analyze --ctu-ast-mode`: the default is not `parse-on-demand`

[ctu-analysis.md](ctu-analysis.md)

The help says `(default: parse-on-demand)`. Without the option, the AST files
are written, i.e. the `load-from-pch` behavior.

```sh
cd ~/demo
CodeChecker analyze compile_commands.json -o h11 --analyzers clangsa --ctu-collect
find h11/ctu-dir -name '*.ast' | wc -l                     # 3
rm -rf h11
CodeChecker analyze compile_commands.json -o h11 --analyzers clangsa --ctu-collect --ctu-ast-mode parse-on-demand
find h11/ctu-dir -name '*.ast' | wc -l                     # 0 (invocation-list.yml instead)
```

### 52. `analyze --file` with a header works only for an absolute path

[skip-and-file-filters.md](skip-and-file-filters.md)

The help says `--file` can contain globs and that a header selects the
sources that include it. Header selection works only for an existing
**absolute** header path. A relative header path finds no dependents, and a
header glob is treated as a plain pattern.

```sh
cd ~/demo
CodeChecker analyze compile_commands.json -o h6 --analyzers clangsa --file ~/demo/lib/util.h
# analyzes main.c and util.c
CodeChecker analyze compile_commands.json -o h6 --analyzers clangsa --file lib/util.h
# [WARNING] - No analysis is required.   (also for "*/util.h" and "*/lib/*.h")
```

### 53. `analyze --disable-all` does not disable all checkers

[checker-selection.md](checker-selection.md)

The help says *"Disable all checkers of all analyzers. It is equivalent to
using "--disable default" as the first argument."* The second sentence is
what happens: only the `default` profile is disabled. Checkers outside the
default profile were already disabled, so the result is the same, unless
`--enable-all` is also given.

```sh
cd ~/demo
CodeChecker analyze compile_commands.json -o h12 --analyzers clangsa --enable-all --disable-all
```

`h12/metadata.json` lists 21 enabled Clang SA checkers
(`nullability.NullableDereferenced`, `optin.core.EnumCastOutOfRange`, ...).

### 54. `cmd sum -n`: run names separated by `:` find nothing

[cmd-sum.md](cmd-sum.md)

The help says the format is `<run_name_1>:<run_name_2>:<run_name_3>`. That
finds no runs; separate the names with spaces.

```sh
CodeChecker cmd sum -n demo:demo2 --url localhost:8001/Default
# [WARNING] - No runs were found!   (exit 1)
CodeChecker cmd sum -n demo demo2 --url localhost:8001/Default
# works
```

### 55. `tu_collector -f` with `-z`: absolute patterns do not match

[tu-collector.md](tu-collector.md)

The help gives `/path/to/*/files` as an example. The pattern is matched
against the raw `file` field of the compilation database, which is often
relative (`main.c`).

```sh
cd ~/demo
tu_collector -l compile_commands.json -z h5.zip -f "$HOME/demo/*"   # no sources collected
tu_collector -l compile_commands.json -z h5.zip -f "*main.c"        # main.c collected
```

### 56. `cmd diff -o html` without `-e`: the error names `parse` options

[local-diff.md](local-diff.md)

```sh
cd ~
CodeChecker cmd diff -b demo/reports -n demo2/reports --new -o html
# [ERROR] - Argument --export not allowed without argument --output when exporting to HTML.   (exit 1)
```

The `cmd diff` options are `-o/--output` (format) and `-e/--export` (folder).

### 57. `analyzers --dump-config`: not only clang-tidy supports it

[analyzers.md](analyzers.md)

The help says *"Currently only clang-tidy supports this option."* For
`clangsa` it prints Clang's analyzer option help (exit code 0):

```sh
CodeChecker analyzers --dump-config clangsa
# OVERVIEW: Clang Static Analyzer Checker and Package Option List
# ...
```

### 58. `checkers -w` prints the normal checker list

[checkers.md](checkers.md)

The help says *"DEPRECATED. Show available warning flags."* The output has the
same lines as plain `checkers` (only the order of the Cppcheck checkers
differs between runs).

### 59. `analyzers --all` and `--details` do nothing

[analyzers.md](analyzers.md)

Both are marked `DEPRECATED`. `CodeChecker analyzers --all --details`
prints exactly the same as `CodeChecker analyzers`.

---

## Environment notes (not CodeChecker bugs)

- Statistical analysis needs Clang's `statisticsCollector` checkers, which
  upstream Clang 19 lacks
  (`clang -cc1 -analyzer-checker-help | grep -i statisticscollector` finds
  nothing).
- On Debian, `clang-apply-replacements` and `clang-extdef-mapping` are only in
  `/usr/lib/llvm-19/bin` (or with a `-19` suffix in `/usr/bin`), not on
  `PATH`.
- The installed `bin/CodeChecker` needs the Python packages of the
  CodeChecker repository's development virtual environment (`venv_dev`, e.g.
  `argcomplete`, `lxml`). Without it: `ModuleNotFoundError: No module named
  'argcomplete'`.
