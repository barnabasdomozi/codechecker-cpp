# Reference: `CodeChecker store`

Usage: [../usage/store.md](../usage/store.md)

Python source: `web/client/codechecker_client/cli/store.py` (client),
`web/client/codechecker_client/blame_info.py`,
`web/server/codechecker_server/api/report_server.py`
(`massStoreRun`, `massStoreRunAsynchronous`, `getMissingContentHashes`,
`getMissingContentHashesForBlameInfo`, `getAnalysisStatisticsLimits`,
`allowsStoringAnalysisStatistics`, `storeAnalysisStatistics`),
`web/server/codechecker_server/api/mass_store_run.py` (server-side
storage), `web/server/codechecker_server/metadata.py`
(`MetadataInfoParser`, `checker_is_unavailable`),
`web/server/codechecker_server/api/report_annotations.py`.

Related: report model and result files
([report-format.md](report-format.md)), hashes
([report-hash.md](report-hash.md)), review statuses
([review-status-and-suppression.md](review-status-and-suppression.md)),
skip files ([skip-and-file-filters.md](skip-and-file-filters.md)),
background tasks ([cmd-serverside-tasks.md](cmd-serverside-tasks.md)),
database tables ([server-database.md](server-database.md) §4.2), client
connection ([server-api.md](server-api.md)).

## 1. Command line

```
CodeChecker store [INPUT ...] [-t/--type/--input-format plist] [-n NAME]
                  [--tag TAG] [--description TEXT] [--trim-path-prefix [P ...]]
                  [-j JOBS] [--zip-loc PATH] [--detach] [--config FILE]
                  [-f/--force] [--url PRODUCT_URL] [--verbose L]
```

| Option | Default |
|---|---|
| `INPUT` | `~/.codechecker/reports` (files or folders) |
| `-t` | `plist` (only choice; `.sarif` files are read too) |
| `-j` | number of CPUs |
| `--url` | `localhost:8001/Default` |
| `-n`, `--tag`, `--description`, `--trim-path-prefix`, `--detach`, `-f` | absent |

`--config` reads the `store` section
([common-infrastructure.md](common-infrastructure.md) §5). Environment:
`CC_PASSWORD`, `CC_PASS_FILE`, `CC_SESSION_FILE`
([server-api.md](server-api.md) §4), `CC_FORCE_SYNC_STORE` (§3.3).

## 2. Client flow

1. Logging to stdout. `--config` file missing → ERROR, exit 1. zlib check.
2. Make every input absolute; a missing input → ERROR `Input path '<p>' does
   not exist!`, exit 1.
3. Run name (§2.1). INFO `Storing analysis results for run '<name>'`. With
   `-f`: INFO `argument --force was specified: the run with name '<name>'
   will be deleted`.
4. Results client and product client for the product URL
   ([server-api.md](server-api.md) §6; the product client checks that the
   product exists).
5. Temporary directory `mkdtemp(suffix="-store", dir=<--zip-loc or first
   input>)`. If that directory is not writable (`PermissionError`), use the
   system temp dir instead; failing again → ERROR `Permission denied! You do
   not have sufficient permissions to create the system temporary
   directory.`, exit 1. A `--zip-loc` that does not exist is **not** handled:
   `FileNotFoundError` traceback, exit 1. Inside it, a temporary `*.zip`.
6. Build the zip (§2.2). Any exception → its text and traceback, ERROR
   `Failed to assemble zip file.`, exit 1. Report limit exceeded → exit 1.
7. If the compressed zip is larger than 1 GiB (1024³ bytes): ERROR `The
   result list to upload is too big (max: 1.0 GiB): <size>.`, exit 1.
8. Base64-encode it; empty → INFO `Zip content is empty, nothing to store!`,
   exit 1.
9. INFO `Storing results to the server ...` and upload (§3).
10. Always: remove the zip and the temporary directory. Any other exception
    in steps 7–9 → traceback, ERROR `Storing the reports failed: <msg>`,
    exit 1.

### 2.1 Run name

`-n` if given. Otherwise from each input **folder** that has a
`metadata.json`:
- version ≥ 2: the `run_name` of the **last** entry of `tools` (an empty
  `tools` list makes the code fail);
- version 1: `name`;
- missing or empty → `"unnamed result folder"`.

One distinct name other than `"unnamed result folder"` → that name. Several
distinct names → `"multiple projects: <n1>, <n2>, ..."` (set order). Nothing
usable → ERROR `No suitable name was found in the inputs for the analysis
run. Please specify one by passing argument --name run_name in the
invocation.`, **exit 2**.

### 2.2 Building the zip (`assemble_zip`)

1. Find result files: for every input, walk folders recursively and take
   files ending in `.plist` or `.sarif`; a file input is used directly
   ([report-format.md](report-format.md) §3). For every directory visited
   by the walk (and the parent directory of a file input) — the "report
   folder", key = its path — remember to include, if present: `metadata.json`, `skip_file` (INFO `Found skip file <path> with
   the following content:\n<content>`), `review_status.yaml`, and the
   folder `conf/` (all its direct entries).
2. Parse all result files (`-j` processes; `-j 1` = in-process). A result
   file whose reports **all** have report hash `0` → ERROR `All bug hashes
   are 0 in the report file, which is no longer supported. Please re-analyze
   the project using CodeChecker!`, exit 1. INFO `Processing report files
   done.`
3. For each report:
   - if it has changed files ([report-format.md](report-format.md) §4):
     collect them and skip the report;
   - else, if its **path hash** ([report-hash.md](report-hash.md)) was not
     seen yet: keep it under (folder of its result file, analyzer name);
   - collect all its original files (main + path/event/note/macro files)
     and remember `(main file original path → set of report lines)`.
4. Write the kept reports of each (folder, analyzer) to a new plist
   `<tmp>/<uuid4>-<analyzer or "unknown">.plist` in a temporary
   `*-unique-plists` directory; these replace the original result files in
   the zip.
5. If any changed files were found: WARNING `The following source file
   contents changed or missing since the latest analysis:\n - <f>\n...\n
   Please re-analyze your project to update the reports!` and exit 1.
6. No source files at all → WARNING `There is no report to store. After
   uploading these results the previous reports become resolved.` (the
   store continues).
7. SHA-256 of every source file's bytes. INFO `Get missing file content
   hashes from the server...`; `getMissingContentHashes(hashes)` → hashes
   the server does not have. INFO `Get file content hashes which do not have
   blame information from the server...`;
   `getMissingContentHashesForBlameInfo(hashes)`. (Both calls are skipped
   for an empty list.)
8. INFO `Collecting review comments ...`: for every main file whose content
   the server **already has**, scan the report lines for review-status
   comments ([review-status-and-suppression.md](review-status-and-suppression.md)
   §3; misspelled comments are WARNING-logged). Files with any comment are
   added to the "needed" set anyway, so the server can read the comments.
9. INFO `Building report zip file...` and write the zip (ZIP64 allowed):

   ```
   reports/<md5 hex of the report folder path>/<file name>   files of step 1 and 4
   reports/<md5>/conf/<entry>                               conf/ contents
   root/<absolute source path without leading '/'>          needed sources
   blame/<absolute source path without leading '/'>         blame JSON (§2.3)
   content_hashes.json                                      {"<abs path>": "<sha256>"} for all sources
   ```

   INFO `Building report zip file (<zip>) done.`
10. Print the summary to stdout:

    ```
    ----======== Summary ========----
    <table: Number of processed analyzer result files, Number of analyzer reports,
            Number of source files, Number of source files with source code comments,
            Number of blame information files>
    ----=================----
    ```
    (statistics count kept reports, files written under `root/`, files
    added in step 8, blame files).
11. Report limit: `getCurrentProduct().reportLimit`; if the number of
    distinct path hashes is larger → ERROR `Report Limit Exceeded` (with an
    explanatory text and `Configured report limit for this product: <n>`),
    exit 1.
12. Compress the whole zip file with zlib (level 9) in place. INFO
    `Compressing report zip file done (<size> / <compressed size>).`

### 2.3 Blame information

For each needed-blame file (hash returned in step 7): find the Git
repository containing its real path (files ignored by Git are skipped);
collect, in parallel:

```json
{"version": "v1", "tracking_branch": "<upstream branch or HEAD commit>",
 "remote_url": "<first URL of the default remote or null>",
 "commits": {"<sha>": {"author": {"name", "email"}, "summary", "message",
                       "committed_datetime": "<str>"}},
 "blame": [{"from": <first line>, "to": <last line>, "commit": "<sha>"}]}
```

(incremental blame of `HEAD`). Without Git support: WARNING `Failed to
collect blame information. Make sure Git is installed on your system.`
INFO `Collecting blame information... Done.` or `No blame information
found for source files.`

## 3. Upload

### 3.1 Asynchronous store (default)

1. `massStoreRunAsynchronous(b64zip, SubmittedRunOptions{runName, tag,
   version = client package version (6.30.0), force, trimPathPrefixes,
   description})` → task token. INFO `Reports submitted to the server for
   processing.`
2. If `allowsStoringAnalysisStatistics()`: upload analysis statistics
   (§3.4).
3. `--detach`: WARNING `Exiting the 'store' subcommand as '--detach' was
   specified: ...\n\n\tCodeChecker cmd serverside-tasks --token <token>
   --await`, print the token on stdout, exit 0.
4. Otherwise poll the task until it ends (`await_task_termination`,
   [cmd-serverside-tasks.md](cmd-serverside-tasks.md) §4). `COMPLETED` →
   INFO `Storing the reports finished successfully.`, exit 0. Other final
   states → ERROR `Storing the reports failed! The job terminated in status
   '<STATUS>'. The comments associated with the failure are:\n\n<comments>`,
   exit 1.

### 3.2 Errors at submission

Checked by the API handler before a task exists (all `RequestFailed`, so
the client prints them via the call wrapper and exits 1):

| Condition | Code, message |
|---|---|
| no `PRODUCT_STORE` | `UNAUTHORIZED`, `You are not authorized to execute this action.` |
| empty run name | `GENERAL`, `A run name is needed to know where to store!` |
| server has 0 task workers | `GENERAL`, `No task worker process is available!` |
| run limit: product `run_limit` (if set) else `max_run_count`; the run does not exist yet and the product already has ≥ limit runs | `GENERAL`, `You reached the maximum number of allowed runs (<count>/<limit>)! Please remove at least <count - limit + 1> run(s) before you try again!` |
| run lock held and not expired (§4.1) | `DATABASE`, `The run named '<name>' is being stored into by <user or "another user">. If the other store operation has failed, this lock will expire at '<time>'.` |
| lock race | `DATABASE`, `The run named '<name>' is being stored into by another user.` or `Someone is already storing to the same run. ...` |
| decompressed zip > 16 GiB | `IOERROR`, `Rejected storage of run '<name>', decompressed ZIP file is too large!` |
| zip empty after decoding | `GENERAL`, `The uploaded ZIP file is empty!` |

Then the handler allocates a task (kind
`report_server::massStoreRunAsynchronous()`, summary `Store of results to
'<product endpoint>' - '<run name>'`, user = logged-in user or
`Anonymous`, product), extracts the zip into
`<workspace>/codechecker_tasks/<machine id with '"<>:\/|*?. → _>/<token>-XXXX/store_zip/`,
writes `store_configuration.json` (`client_version`, `force_overwrite`,
`path_prefixes_to_trim`, `run_name`, `run_description`, `store_tag`,
`user_name`) next to it, enqueues the task and returns the token. Note: the
run lock taken here stays until the task ends.

### 3.3 Synchronous store (`CC_FORCE_SYNC_STORE`)

If `CC_FORCE_SYNC_STORE` is `y`, `yes`, `t`, `true`, `on` or `1`
(case-insensitive), the legacy `massStoreRun(name, tag, version, b64zip,
force, trimPathPrefixes, description)` is used: the server does the same
as above (task kind `report_server::massStoreRun()`, summary `Legacy store
of results to ...`, plus a task comment `WARNING!\nExecuting a legacy
'massStoreRun()' API call!`), then blocks until the task ends. A failed
task → `GENERAL "massStoreRun()'s processing failed. Here follow the
details:\n\n<comments>"`; cancelled → `GENERAL "Server administrators
cancelled the processing of the massStoreRun() request!"`; otherwise it
returns `-1`. The client wraps the call in a 1-hour watchdog (`SIGUSR1`);
on timeout it logs where it was waiting and ERROR `Timeout!\n\tThe
server's reply did not arrive after 3600 seconds ...`, exit 1. Then
analysis statistics (§3.4) and exit 0 (no "finished successfully" line).

### 3.4 Analysis statistics upload

Only if the server has `store.analysis_statistics_dir` configured
(`allowsStoringAnalysisStatistics()`). Errors here are only debug-logged.

1. `getAnalysisStatisticsLimits()` → `{FAILURE_ZIP_SIZE: n,
   COMPILATION_DATABASE_SIZE: m}` (keys present only if configured).
2. For every input (folder: only its direct files and directories):
   - `compile_cmd.json` if not larger than the compilation database limit;
   - `metadata.json`, `compiler_info.json`; `compiler_includes.json` and
     `compiler_target.json` only if there is no `compiler_info.json`;
   - files of `failed/` while the running total size stays within the
     failure zip limit.
   If no failure zip was taken, nothing is uploaded.
3. Zip them (paths as given, absolute), zlib-compress, base64,
   `storeAnalysisStatistics(run name, b64)`. The server writes the
   decompressed zip to `<analysis_statistics_dir>/<product endpoint>/<slugified run name>.zip`
   (directory mode 0740, file mode 0640) and returns true.

## 4. Server-side processing (task worker)

The task worker connects to the product (status not `OK` → task fails with
`Database for product '<ep>' is in a bad shape!`) and runs the steps below.
Between steps (and per source file / result file) it updates the task
heartbeat and checks for cancellation ([cmd-serverside-tasks.md](cmd-serverside-tasks.md)).
Progress is logged at INFO as `[<run>] <step>...` and
`[<run>] <step>. Done. (Duration: <s> sec)`.

### 4.1 Run lock

`run_locks` row per run name. Taken at submission: no row → insert
`(name, now, user)`; a row older than 30 minutes → reuse it (touch, set
user); otherwise refuse (§3.2). During the database transaction of §4.5
the row is selected `FOR UPDATE NOWAIT`. Removed in all cases when the
task ends (also on failure).

### 4.2 Source files

For each `(path, hash)` of `content_hashes.json`:
- stored path = path with `--trim-path-prefix` applied
  ([report-format.md](report-format.md) §1: longest matching prefix);
- if `root/<path>` is in the zip: insert `file_contents(hash, zlib(content))`
  unless it exists, then the file record;
- otherwise only the file record (the content must already exist; if not:
  ERROR `File ID for <path> is not found in the DB with content hash <h>.
  Missing from ZIP?` and the path gets no id).

File record: `files` row with `(filepath = stored path, content_hash)`;
reused if it exists, else inserted with `filename` = base name, and the new
file is assigned to source components by shell-glob matching
([cmd-results.md](cmd-results.md) §3.1). Concurrent inserts are tolerated.

Blame: for every file under `blame/`, path `/<relative path>`: set
`file_contents.blame_info = zlib(JSON without remote_url and
tracking_branch)` where it is still NULL for that path's hash, and set
`files.remote_url` / `tracking_branch` where `remote_url` is NULL for that
(untrimmed) path.

### 4.3 Metadata

Every directory under `reports/` gets a parsed `metadata.json` (or an empty
one). From version 2 (`tools` list):
- CodeChecker version: `version` of tools named `codechecker` (joined with
  `"; "`);
- check commands: `" ".join(command)` of each tool (distinct);
- durations: `end - begin` of each tool's `timestamps`;
- per analyzer (from `tool.analyzers`, or the tool itself if it has none):
  statistics (`failed`, `failed_sources`, `successful`,
  `successful_sources`, versions) and checkers `{name: enabled}`; failed and
  successful counts are recomputed from the distinct source lists;
- if `num_of_report_dir > 1`: the checker map is emptied (so `off` and
  `unavailable` cannot be computed). 6.30.0 clients never produce this
  (each report folder keeps its own metadata).

Version 1: `command` (list → joined), `timestamps`,
`versions.codechecker`, `analyzer_statistics`, `checkers` (a list of names
means all enabled).

Derived: enabled checkers, disabled checkers, checker → analyzer map.

### 4.4 Checker identifiers

Ensure a `checkers` row exists for every `(analyzer, checker)` of all
metadata files, plus `__FAKE__/__FAKE__` and `UNKNOWN/NOT FOUND`. New rows
get severity = the label severity of the checker **name** (looked up
without the analyzer, [checker-labels.md](checker-labels.md)). Up to 3
attempts on database contention (waiting 30 s, then 60 s); then the task
fails with `Storing the names of the checkers in the run failed due to
excessive contention!`.

### 4.5 Run, history, reports (one transaction)

`run_history_time = now` (taken before the metadata step).

**Run** (`runs`):
- exists and `-f`: delete it (cascades to its reports and histories); a
  run with `can_delete = false` fails the task with `Can't delete <id>`;
  INFO `Removing previous analysis results...`; insert a new run;
- exists: `date = now`, `duration = -1`;
- else insert `(name, version = client version, date = now,
  duration = -1)`.

**History**: if `--tag` is given and a history row of this run already has
that tag, its tag is cleared. Insert `run_histories(run_id, version_tag,
user, time = run_history_time, cc_version = metadata CodeChecker versions
joined with "; " or NULL, description)`.

**Analyzer statistics** (one row per analyzer type over all metadata):
`version` = zlib(versions joined `"; "`), `failed_files` = zlib(failed
sources joined `\n`), `failed` = number of failed sources, `successful` =
number of successful sources (or the summed `successful` count when no
list exists). For client version `6.9.0` the failed list is the single
text `Unavailable in CodeChecker 6.9.0!`.

**Analysis info**: for every report folder and each of its check commands:
checker ids of its enabled and disabled checkers → checker set (reuse by
hash or create with items, [server-database.md](server-database.md) §4.2);
`analysis_info(zlib(command), checker_set_id)` linked to the history; the
folder's `conf/*` files are stored as `file_contents` and
`analysis_info_files(analysis_info_id, file name, hash)`.

**Reports**: load all current reports of the run, grouped by report hash
(`old`). Then walk `reports/` recursively; per directory: its `skip_file`
(if present), its `review_status.yaml` (if present), its metadata; for
every result file (`.plist`, `.sarif`):

1. Parse it. For each report:
   1. apply `--trim-path-prefix`;
   2. any of its files without a file id → WARNING `Failed to get database
      id for file path '<paths>'! Skip adding report: <file>:<line>:<col>
      [<checker>]`, skip;
   3. remember its checker name as "seen";
   4. skip if the skip file matches the main file's original path;
   5. skip if its path hash was already stored in this store;
   6. detection status: hash in `old` → `reopened` if the first old
      report's status is `resolved`, else `unresolved`; `detected_at` = the
      old report's; otherwise `new`, `detected_at = run_history_time`;
   7. analyzer name = metadata checker→analyzer map, else the report's own;
   8. review status from source comments / the folder's rules file
      ([review-status-and-suppression.md](review-status-and-suppression.md)
      §2, reading sources from `root/` in the zip); an invalid comment
      (e.g. two comments for one report) leaves `unreviewed` and records
      the error; author = storing user, date = `run_history_time`;
   9. `fixed_at`: for `false_positive`/`intentional`, the old report's
      `fixed_at` if the old one was also FP/intentional, else
      `run_history_time`; otherwise NULL;
   10. report limit: if the number of reports added in this store reaches
       the product's `report_limit` → the task fails with `**Report Limit
       Exceeded** ... Limit: <n>`;
   11. insert `reports(file_id, run_id, bug_id = report hash, checker_id
       (the checker row, or `__FAKE__` if it does not exist yet), line,
       column, path_length = number of bug path events, checker_message,
       detection_status, review_status, author, message, date, is_in_source,
       detected_at, fixed_at)` and link it to the folder's analysis info.
2. For every added report: `report_path_data` (path positions, events,
   notes, macro expansions; [server-database.md](server-database.md)
   §4.2), `report_path_data_files` for every file involved, and annotations
   (§4.6).
3. **Review status rules**: for added reports whose status does **not**
   come from a source comment and whose hash has a `review_statuses` row:
   set `review_status` and `review_status_message` from the rule,
   `is_in_source = false`, and `fixed_at` as in step 1.9. (The rule's author
   and date are meant to be copied too, but the code assigns misspelled
   attributes, so the report keeps the storing user and store time.)
4. INFO `[<run>] Processed <n> analyzer result file(s).`
5. Enabled checkers := union of all folders' enabled checkers ∪ seen
   checkers; disabled := union of disabled − seen.
6. Old reports: if their hash was stored again they are **deleted** (the
   new rows replace them, so report ids change on every store). Every other
   old report gets
   - `off` if its checker name is in the disabled set (this wins: a checker
     disabled in one folder and enabled in another counts as disabled —
     verified);
   - else `unavailable` if the enabled set is non-empty, the checker is not
     in it and its name does not start with `clang-diagnostic-`;
   - else `resolved`;
   and `fixed_at = run_history_time` if it was NULL.

   Without `-f` this applies to **all** old reports of the run, also those
   of files not analyzed this time (the help text promises otherwise).

Commit. Then, if some reports got the `__FAKE__` checker (checkers missing
from `metadata.json`, e.g. converter output): create their real checker
rows (§4.4) and update those reports' `checker_id` in a second transaction.

### 4.6 Annotations

Allowed keys: `timestamp` (ISO date-time, stored normalised as
`YYYY-MM-DD HH:MM:SS[.ffffff]`), `testcase` (string),
`chronological_order` (integer, stored as its decimal text). ⚠️ An unknown
key or a badly formatted value is meant to raise
`'<key>' is not an allowed report annotation.` / `'<value>' has wrong
format. '<key>' annotations must be '<display>'.`, but the code uses a
removed error code (`REPORT_FORMAT`), so the task fails with
`type object 'ErrorCode' has no attribute 'REPORT_FORMAT'` instead.

### 4.7 Finishing

1. `runs.duration` = integer part of the sum of all metadata durations
   (not the wall-clock time).
2. Config DB: `products.num_of_runs += 1` for a new run (not for updates,
   also not after `-f`), `latest_storage_date = run_history_time`.
3. INFO `User '<user>' stored results to product '<display name>', run
   '<name>' in <s> seconds (run id: <id>[, under tag: '<tag>'], report
   count: <n>, decompressed size: <KiB> KiB).`
4. Release the run lock.
5. ⚠️ If invalid source-code comments were found, the task now fails —
   **after** the data was committed — with `One or more source files
   contained invalid source code comments! Failed to set review
   statuses.` followed by a table. In 6.30.0 building that table itself
   crashes (each error message is split on `|` into 3 columns, but the
   messages contain no `|`), so the task's failure comment is
   `Row has incorrect number of values, (actual) 1!=3 (expected)`
   (verified). Either way `store` exits 1 while the run is stored.

Any exception fails the task; its message becomes the task comment
`FAILED!\nException during execution:\n<message>` (plus a traceback
comment at debug level) and the client prints it (§3.1).

## 5. Exit codes

| Code | When |
|---|---|
| 0 | stored (or submitted with `--detach`) |
| 1 | any error, including server-side failures and the cases in §4.7 step 5 |
| 2 | no run name could be determined (§2.1); malformed product URL ([server-api.md](server-api.md) §3.1) |
