# Reference: server databases, schema, status and upgrade

Usage: [../usage/server-database.md](../usage/server-database.md)

Python source: `web/server/codechecker_server/database/database.py`
(`SQLServer`, `SQLiteDatabase`, `PostgreSQLServer`, status checks),
`web/server/codechecker_server/database/config_db_model.py`,
`web/server/codechecker_server/database/run_db_model.py`,
`web/server/codechecker_server/database/db_cleanup.py`,
`web/server/codechecker_server/migrations/{config,report}/versions/*.py`
(Alembic migrations), `web/server/codechecker_server/cli/server.py`
(`server_init_start`, `check_product_db_status`, `__db_status_check`,
`__db_migration_multiple`, `print_prod_status`),
`web/codechecker_web/shared/database_status.py`,
`web/codechecker_web/shared/host_check.py`,
`web/codechecker_web/shared/pgpass.py`.

## 1. Databases

| Database | Content | Location |
|---|---|---|
| Configuration database | products, permissions, sessions, tokens, background tasks, server settings | `--sqlite` (default `<workspace>/config.sqlite`) or `--postgresql` + `--db-*` |
| Product ("run") database, one per product | runs, reports, files, comments, review status rules, components, ... | connection string stored in the config DB `products.connection` |

Engines: SQLite and PostgreSQL. A port that wants to open databases created
by the reference implementation must use the same schema (§4) and the same
Alembic revision ids (§6).

## 2. Connection strings

SQLAlchemy URLs:

- SQLite: `sqlite+pysqlite:///<path>`. The config DB path is made absolute
  against the current directory. Product paths may be **relative**; they
  are resolved against the workspace directory (the `Default` product is
  stored as `sqlite+pysqlite:///Default.sqlite`, so moving the workspace
  keeps working). Every SQLite connection runs `PRAGMA foreign_keys=ON`;
  busy timeout 600 s.
- PostgreSQL: `postgresql+<driver>://<user>[:<password>]@<host>:<port>/<db>`
  (`?client_encoding=utf8` for psycopg2). Driver: `$CODECHECKER_DB_DRIVER`
  if set, else `psycopg2` if importable, else `pg8000`. Connect timeout 60 s
  (psycopg2). Passwords are not given on the command line: libpq reads
  `~/.pgpass` or `$PGPASSFILE` (psycopg2); for pg8000 the server parses
  `$PGPASSFILE` itself (libpq format `host:port:db:user:password`, `*`
  wildcard per field, `\` escapes, first matching line).
- No connection pooling (a new connection per session).
- The location shown to users: SQLite → file path; PostgreSQL →
  `host:port/database` (no user or password).

If the needed driver cannot be imported: ERROR `The selected SQL driver is
not available!` and an `IOError`.

## 3. Status, start-up checks and the database options

### 3.1 Status values

`DBStatus` (`shared.thrift`) and the messages printed:

| Value | Name | Message |
|---|---|---|
| 0 | `OK` | `Database is up to date.` |
| 1 | `MISSING` | `Database is missing.` |
| 2 | `FAILED_TO_CONNECT` | `Connection failed to the database server.` |
| 3 | `SCHEMA_MISMATCH_OK` | `Database schema mismatch! Possible to upgrade.` |
| 4 | `SCHEMA_MISMATCH_NO` | `Database schema mismatch! NOT possible to upgrade.` |
| 5 | `SCHEMA_MISSING` | `Database schema is missing.` |
| 6 | `SCHEMA_INIT_ERROR` | `Schema initialization error.` |
| 7 | `SCHEMA_UPGRADE_FAILED` | `Schema upgrade failed.` |

`check_schema()`:
1. Connect (failure → `FAILED_TO_CONNECT`; SQLite creates an empty file, so
   a missing SQLite file is `SCHEMA_MISSING`, not `MISSING`).
2. Read `alembic_version.version_num` (no table or no row → `SCHEMA_MISSING`).
3. Equal to the package head (§6) → `OK`. Otherwise: one of the package's
   known revisions → `SCHEMA_MISMATCH_OK`; unknown (a newer database) →
   `SCHEMA_MISMATCH_NO`.

`connect(init=True)`: if there is no revision yet, create the schema by
running all migrations up to the head, then `check_schema()`. Errors →
`SCHEMA_INIT_ERROR`.

`upgrade()`: only if `check_schema()` is `SCHEMA_MISMATCH_OK` or
`SCHEMA_MISSING` (else return that status); run the migrations up to the
head in one transaction → `OK` or `SCHEMA_UPGRADE_FAILED`.

### 3.2 Start-up sequence

Done by `CodeChecker server` before the HTTP server starts (also when
`--db-status`/`--db-upgrade-schema` are given):

1. INFO `Checking configuration database ...`; status of the config DB;
   INFO its message.
   - `SCHEMA_MISSING` → initialise it (failure: ERROR `Config database
     initialization failed!`, ERROR `Please check debug logs.`, exit 1).
   - `SCHEMA_MISMATCH_NO` → exit 1 (only debug messages).
   - `SCHEMA_MISMATCH_OK` → WARNING `Please note after migration only newer
     CodeChecker versions can be used to start the server!`, WARNING `It is
     advised to make a full backup of your configuration database!`,
     WARNING `<location>`; ask `Do you want to upgrade to the new schema?
     Y(es)/n(o) ` (answer `y`/`yes`, case-insensitive; skipped with
     `--db-force-upgrade`). Yes → INFO `Upgrading schema ...`, upgrade, INFO
     status message; not `OK` → ERROR `Schema migration failed`, exit with
     the status value. No → INFO `No schema migration was done.`, exit 0.
   - `MISSING` → ERROR `Missing configuration database.`, ERROR `Server can
     not be started.`, exit 1.
2. `--db-status` / `--db-upgrade-schema` (§3.3).
3. `Default` product: created only if the config DB is SQLite, the file
   `<workspace>/Default.sqlite` does not exist **and** the config DB has no
   products. The SQLite file is initialised, a `products` row is added
   (endpoint and display name `Default`, description `Default product
   created at server start.`, connection `sqlite+pysqlite:///Default.sqlite`)
   and INFO `Product 'Default' at '<abs path>' created and set up.`
4. INFO `Connecting to product databases ...`; status of every product
   (ordered by endpoint). A product whose status cannot be determined is
   ERROR `Unable to get the status for product '<ep>', considering as if the
   connection failed.` and `FAILED_TO_CONNECT`.
5. If any product is `SCHEMA_MISMATCH_OK` or `SCHEMA_MISSING`: print the
   status table, WARNING `Multiple products can be upgraded, make a backup!`,
   run the multi-product upgrade (§3.4) for all products, then re-check.
6. Print the status table (§3.5).
7. ⚠️ If the **first** product (by endpoint) is not `OK` (the loop stops after
   one product — the intent was "any product"): ERROR `There are some
   database issues.` and, unless `--db-force-upgrade`, ask `Do you want to
   start the server? Y(es)/n(o) `; no → exit 1.

### 3.3 `--db-status` and `--db-upgrade-schema`

**Intended** behavior (what a port should implement):

- `--db-status P|all`: print the status table (§3.5) for product `P` or all
  products and exit 0. Unknown product → ERROR `No product was found with
  this endpoint: <P>`, exit 1.
- `--db-upgrade-schema P|all`: run §3.4 for `P` or all products and exit
  with its return value (0, or 1 for an unknown product).
- `--db-force-upgrade`: answer every upgrade question with yes.

⚠️ **Actual in 6.30.0**: both options are silently ignored and the server
starts normally (the code passes a non-existent attribute
`context.migration_root`; the resulting `AttributeError` is caught by the
handler meant for "option not given", which logs DEBUG `Status was not in
the arguments.` / `Product upgrade was not in the arguments.`). The start-up
check (§3.2 step 5) still upgrades outdated products.

### 3.4 Multi-product upgrade (`__db_migration_multiple`)

Arguments: an optional list of requested products, and "force".

1. INFO `Preparing schema upgrade for '<p1>', '<p2>'` (or `<all
   products>`). Get all product statuses. A requested product that does not
   exist → ERROR `No product was found with endpoint '<p>'`, return 1.
2. ⚠️ The list is then replaced by **all** products (sorted): even when a
   single product was requested, every product is considered. A port should
   only upgrade the requested ones.
3. WARNING `Please note after migration only newer CodeChecker versions can
   be used to start the server!`, WARNING `It is advised to make a full
   backup of your run databases.`, INFO `========================`.
4. For each product: INFO `Checking: <ep>`; connect; INFO status message;
   - `SCHEMA_MISSING`: ask `Do you want to initialize a new schema for
     '<ep>'? Y(es)/n(o) ` → schedule an init (INFO `[<ep>] Schema will be
     initialised...`) or INFO `[<ep>] No schema initialization will be done.`
   - `SCHEMA_MISMATCH_OK`: ask `Do you want to upgrade '<ep>' to new schema?
     Y(es)/n(o) ` → schedule an upgrade (INFO `[<ep>] Schema will be
     upgraded...`) or INFO `[<ep>] No schema migration will be done.`
   - other statuses: nothing.
   With force, the questions are not asked (yes). Errors are logged and the
   product is skipped.
5. INFO `========================`. If anything is scheduled: INFO
   `Initialising/upgrading products using <n> concurrent jobs...` (n =
   min(scheduled, CPUs)), run them in parallel; each logs INFO `[<ep>]
   Initialising...` or `[<ep>] Upgrading...` and `[<ep>] Done
   initialising|upgrading. <status message>`. Failures → ERROR `The
   following products failed to upgrade: '<ep>' (<message>), ...`; else INFO
   `Schema initialisation(s)/upgrade(s) executed successfully.` INFO
   `========================`.
6. Return 0 (also when some upgrades failed: the server can start and
   refuses requests to broken products, [server-api.md](server-api.md) §2.2).

### 3.5 Status table

Logged at INFO as `Status of products:\n<table>` (`table` format, rows
sorted by the first column). Columns:

`Product endpoint, Database status, Database location, Schema version in the database, Schema version in the package`

- Database status: the message of §3.1.
- Schema version in the database: the revision, or `None` if the status is
  `FAILED_TO_CONNECT`, `MISSING`, `SCHEMA_INIT_ERROR` or `SCHEMA_MISSING`;
  ` (up to date)` is appended when it equals the package head.

## 4. Schema

Naming conventions for constraints and indexes (needed for migrations to
apply): primary key `pk_<table>`, unique `uq_<table>_<first column>`, foreign
key `fk_<table>_<first column>_<referred table>`, check
`ck_<table>_<first column>`, index `ix_<first column label>` (that is
`ix_<table>_<column>`).

Types below are the SQLite rendering. Enum columns hold the **lower-case or
upper-case strings** shown, not numbers. "zlib" = zlib-compressed bytes
(any compression level reads back; the server writes level 9 for path data
and the default level elsewhere).

### 4.1 Configuration database (head `635389f535cd`)

| Table | Columns | Notes |
|---|---|---|
| `products` | `id` INTEGER PK, `endpoint` VARCHAR NOT NULL UNIQUE, `connection` VARCHAR NOT NULL, `display_name` VARCHAR NOT NULL, `description` TEXT, `run_limit` INTEGER, `is_review_status_change_disabled` BOOLEAN DEFAULT 0, `latest_storage_date` DATETIME, `num_of_runs` INTEGER DEFAULT 0, `confidentiality` VARCHAR, `report_limit` INTEGER NOT NULL DEFAULT 500000 | `display_name` and `description` are plain text (the API base64-encodes them). `num_of_runs`/`latest_storage_date` are caches updated by store and delete. Confidentiality: `CONFIDENTIAL`, `INTERNAL`, `OPEN` or NULL. |
| `permissions_system` | `id` PK, `permission` VARCHAR(15) (`SUPERUSER`, `PERMISSION_VIEW`), `name` VARCHAR NOT NULL, `is_group` BOOLEAN NOT NULL | |
| `permissions_product` | `id` PK, `permission` VARCHAR(14) (`PRODUCT_ADMIN`, `PRODUCT_ACCESS`, `PRODUCT_STORE`, `PRODUCT_VIEW`), `product_id` → `products.id` ON DELETE CASCADE, `name`, `is_group` | |
| `auth_sessions` | `id` PK, `user_name` VARCHAR, `token` CHAR(32) NOT NULL UNIQUE, `groups` VARCHAR (`;`-separated), `last_access` DATETIME NOT NULL, `description` VARCHAR, `can_expire_UNUSED` BOOLEAN DEFAULT 1 | |
| `personal_access_tokens` | `id` PK, `user_name`, `token_name`, `token` CHAR(32) NOT NULL UNIQUE, `description`, `groups` (`;`-separated), `last_access` NOT NULL, `expiration` DATETIME, `creation_date` NOT NULL; UNIQUE(`user_name`, `token_name`) | |
| `oauth_sessions` | `id` PK, `provider`, `state`, `code_verifier` (all NOT NULL), `expires_at` | pending OAuth logins |
| `oauth_tokens` | `id` PK, `access_token` NOT NULL, `expires_at`, `refresh_token` NOT NULL, `auth_session_id` → `auth_sessions.id` ON DELETE CASCADE (indexed) | |
| `server_configurations` | `id` PK, `config_key` NOT NULL, `config_value` | only key in use: `notification_banner_text` |
| `background_tasks` | `token` CHAR(64) PK, `machine_id` (idx), `kind` NOT NULL (idx), `status` VARCHAR(9) NOT NULL (idx), `product_id` → `products.id` ON DELETE CASCADE (idx), `summary` NOT NULL, `comments` TEXT, `username`, `enqueued_at`, `started_at`, `finished_at`, `last_seen_at`, `consumed` BOOLEAN NOT NULL DEFAULT 0, `cancel_flag` BOOLEAN NOT NULL DEFAULT 0 | [cmd-serverside-tasks.md](cmd-serverside-tasks.md) |
| `alembic_version` | `version_num` VARCHAR(32) PK | |

### 4.2 Product database (head `feb5f01b5b52`)

| Table | Columns | Notes |
|---|---|---|
| `runs` | `id` PK, `date` DATETIME, `duration` INTEGER (seconds, `-1` while storing), `name` VARCHAR UNIQUE, `version` VARCHAR, `command` VARCHAR, `can_delete` BOOLEAN NOT NULL DEFAULT 1 | `date` = time of the latest store; `version` = the server's package version (`6.30.0`) |
| `run_locks` | `name` PK, `locked_at` DATETIME NOT NULL, `username` | a run being stored ([store.md](store.md)) |
| `run_histories` | `id` PK, `run_id` → runs CASCADE (idx), `version_tag`, `user` NOT NULL, `time` NOT NULL, `check_command` BLOB, `cc_version`, `description`; UNIQUE(`run_id`, `version_tag`) | one row per store; `user` = `Anonymous` without auth |
| `analyzer_statistics` | `id` PK, `run_history_id` → run_histories CASCADE (idx), `analyzer_type`, `version` BLOB (zlib, analyzer versions joined by `"; "`), `successful`, `failed`, `failed_files` BLOB (zlib, `\n`-joined paths) | |
| `analysis_info` | `id` PK, `analyzer_command` BLOB (zlib), `checker_set_id` | |
| `analysis_info_files` | PK(`analysis_info_id` → CASCADE, `filename`, `content_hash` → file_contents CASCADE) | |
| `run_history_analysis_info` | PK(`run_history_id` → CASCADE, `analysis_info_id` → analysis_info) | |
| `report_analysis_info` | PK(`report_id` → reports CASCADE, `analysis_info_id` → analysis_info) | |
| `checkers` | `id` PK, `analyzer_name`, `checker_name`, `severity` INTEGER (idx, `Severity` enum value); UNIQUE(`analyzer_name`, `checker_name`) | rows 1 `__FAKE__/__FAKE__` and 2 `UNKNOWN/NOT FOUND` (severity 0) are inserted by the migrations and always exist |
| `checker_set` | `id` PK, `hash_digest` UNIQUE NOT NULL | digest = SHA-256 hex of `{"d":[sorted disabled checker ids],"e":[sorted enabled ids]}` serialised with sorted keys and no spaces |
| `checker_set_items` | PK(`checker_set_id` → CASCADE, `checker_id` → checkers RESTRICT), `enabled` BOOLEAN NOT NULL | |
| `file_contents` | `content_hash` PK, `content` BLOB (zlib source text), `blame_info` BLOB (zlib JSON) | |
| `files` | `id` PK, `filepath` NOT NULL, `filename` NOT NULL (base name), `content_hash` → file_contents CASCADE (idx), `remote_url`, `tracking_branch`; UNIQUE(`filepath`, `content_hash`) | |
| `reports` | `id` INTEGER/BIGINT PK, `file_id` → files CASCADE (idx), `run_id` → runs CASCADE (idx), `bug_id` VARCHAR (idx, the report hash), `checker_id` INTEGER NOT NULL (idx; see the note below the table), `line`, `column`, `checker_message`, `detection_status` VARCHAR(11) (`new`, `unresolved`, `resolved`, `reopened`, `off`, `unavailable`), `detected_at` NOT NULL, `fixed_at`, `path_length`, `review_status` VARCHAR(14) DEFAULT `unreviewed` (`unreviewed`, `confirmed`, `false_positive`, `intentional`), `review_status_author`, `review_status_date`, `review_status_is_in_source` BOOLEAN DEFAULT 0, `review_status_message` BLOB (UTF-8), plus legacy unused columns `checker_name_MOVED_TO_checkers`, `checker_cat_UNUSED`, `bug_type_UNUSED`, `severity_MOVED_TO_checkers`, `analyzer_name_MOVED_TO_checkers` (NOT NULL DEFAULT `unknown`) | |
| `report_path_data` | `report_id` BIGINT PK → reports CASCADE, `path_data` BLOB NOT NULL | zlib (level 9) of a JSON list of `{"from_row","from_col","to_row","to_col","file_id","type","msg"}` with `type` = `path` (execution path range, `msg` null), `event` (bug path event), `note`, `macro` (macro expansion); written in that order |
| `report_path_data_files` | PK(`report_path_data_id` → report_path_data CASCADE (idx), `file_id` → files CASCADE (idx)) | every file a report touches (its own file and path files) |
| `report_annotations` | PK(`report_id` → reports CASCADE (idx), `key`), `value` NOT NULL | |
| `comments` | `id` PK, `bug_hash` NOT NULL (idx), `author` NOT NULL, `message` BLOB NOT NULL (UTF-8), `created_at` NOT NULL, `kind` INTEGER NOT NULL DEFAULT 0 (`USER`=0, `SYSTEM`=1) | attached to a hash, not a report |
| `review_statuses` | `bug_hash` PK, `status` VARCHAR(14) NOT NULL, `author` NOT NULL, `message` BLOB NOT NULL (UTF-8), `date` NOT NULL | review status rules ([cmd-suppress.md](cmd-suppress.md)) |
| `source_components` | `name` PK, `value` BLOB NOT NULL (UTF-8, `\n`-separated `+`/`-` lines), `description` TEXT, `username` | [cmd-components.md](cmd-components.md) |
| `source_component_files` | PK(`source_component_name` → source_components CASCADE, `file_id` → files CASCADE) | [cmd-results.md](cmd-results.md) §3.1 |
| `cleanup_plans` | `id` PK, `name` NOT NULL UNIQUE, `due_date`, `description`, `closed_at` | |
| `cleanup_plan_report_hashes` | `bug_hash` PK, `cleanup_plan_id` → cleanup_plans CASCADE (idx) | |
| `filter_presets` | `id` PK, `preset_name` VARCHAR(100) NOT NULL UNIQUE, `report_filter` JSON NOT NULL | [cmd-filter-preset.md](cmd-filter-preset.md) |
| `alembic_version` | `version_num` VARCHAR(32) PK | |

Datetimes are naive local time of the server, stored by SQLite as
`YYYY-MM-DD HH:MM:SS.ffffff` text. Foreign keys in the product DB are
`DEFERRABLE INITIALLY DEFERRED` unless noted otherwise
(`report_annotations`, `source_component_files`,
`report_analysis_info.analysis_info_id` and
`run_history_analysis_info.analysis_info_id` are immediate).
`reports.checker_id`: on PostgreSQL a real foreign key
`fk_reports_checker_id_checkers` without a default; on SQLite only an index
with `DEFAULT 1` (the migration could not add the constraint without
rebuilding the table).

## 5. Garbage collection

At server start, unless `--skip-db-cleanup`, all products are cleaned in
parallel: INFO `Performing database cleanup using <n> concurrent jobs...`
(n = min(products, CPUs)). Per product (products whose status is not `OK`
are skipped: `Cleanup not attempted, database status is "<message>"`), INFO
`[<ep>] Garbage collection started...`, then in this order (each step in
its own transaction; database errors are logged as ERROR and the next step
runs):

1. **Expired run locks**: delete `run_locks` with `locked_at` older than
   30 minutes.
2. **Unused files**: delete `files` rows not referenced by
   `report_path_data_files` (in chunks of 500 000 ids), then
   `file_contents` rows whose hash is used by neither `files` nor
   `analysis_info_files`.
3. **Unused comments**: delete comments whose `bug_hash` matches no report.
4. **Unused analysis info**: delete `analysis_info` rows referenced by
   neither `run_history_analysis_info` nor `report_analysis_info` (on
   PostgreSQL the two foreign keys are dropped during the delete and
   re-created afterwards).
5. **Unused checker sets**: delete `checker_set` rows not referenced by any
   `analysis_info.checker_set_id`.
6. **Severity upgrade**: for each analyzer in the server's checker label
   database (sorted) and each `checkers` row of that analyzer: compare the
   stored severity with the label severity
   ([checker-labels.md](checker-labels.md)). An invalid stored value is
   ERROR-logged and treated as `UNSPECIFIED` (0). If the label severity is
   `UNSPECIFIED` (no entry): WARNING `[<ep>] Checker '<a>/<c>' (database
   severity: '<S>' (<n>)) does not have a corresponding entry in the label
   config file.`, then try the names `<a>.<c>`, `<a>-<c>`, `<a>/<c>`: none
   found → keep; several with different severities → ERROR and keep; one
   severity → INFO `Found similar checker ...` and use it. If the result
   differs: INFO `[<ep>] Upgrading the severity of checker '<a>/<c>' from
   '<old>' (<n>) to '<new>' (<m>).` and update the row.

Failures → ERROR `Failed to perform automatic cleanup on <n> products!
Earlier logs might contain additional detailed reasoning.\n\t* '<ep>'
(<reason>)`. INFO `[<ep>] Garbage collection finished.`

Config DB clean-up (also skipped by `--skip-db-cleanup`): delete
`auth_sessions` whose `last_access` is older than `session_lifetime`
seconds.

## 6. Migrations

Revisions, oldest first. The last one is the package head.

Configuration database (19):

```
8268fc7ca7f4 initial_schema
150800b30447 share_sessions_through_the_database
bb5278995f41 run_limitation_for_products
6b9f832d0b20 add_user_name_and_group_to_session
4964142b58d2 authentication_session_tokens
3335ff7593cc disable_review_status_change
dec6feb991e6 new_table_for_server_config
302693c76eb8 remove_db_version_table
126fa3f55e4b added_permission_for_product_view
4db450cf38af add_extra_product_detail_columns
cf025b6d7998 add_confidentiality
7829789fc19c global_permission_to_get_access_controls
00099e8bc212 store_limit
f59dfe4623fa clear_legacy_web_sessions
04cd4df82fb5 adds_oauth_sessions_and_oauth_tokens_
7ed50f8b3fb8 new_table_for_personal_access_tokens
5e1501dfd333 implemented_keeping_track_of_background_
511b1b37de2e add_pat_creation_date_column
635389f535cd add_index_oauth_tokens_auth_session_id
```

Product database (30):

```
82ca43f05c10 initial_schema
4b38fa14c27b file_id_index_for_reports
dd9c97ead24  share_the_locking_of_runs
101a9cb747de add_bug_event_and_point_report_index
080349e895d7 add_check_command_to_run_history
3793e361a752 source_components
e89887e7d3f0 add_bug_path_length
9987aa593ca7 add_codechecker_version_to_run_history
39f9e96071c0 analyzer_statistics
c042e02cca99 extended_report_data
3e91d0612422 off_and_unavailable_detection_statuses
6cb6a3a41967 system_comments
a79677f54e48 remove_db_version_table
5f8a443a51e5 add_description_for_run_history
2185167f8568 content_hash_index_for_files
af5d8a21c1e4 add_analyzer_name_for_report
dabc6998b8f0 analysis_info_table
a24461972d2e add_index_for_report_and_history_id_columns
f8291ab1d6be fix_setting_analysis_info_id_seq
ad2a567e513a git_blame_info
fb356f0eefed cleanup_plan
75ae226b5d88 review_status_for_each_report
9d956a0fae8d report_annotations
c3dad71f8e6b store_information_about_enabled_and_disabled_checkers_for_a_run
198654dac219 add_sourcecomponentfile_join_table
24c9660f82b1 add_filter_presets_table
a7f3c8e21b90 report_id_bigint
b1be74589382 add_checkerset_checkersetitem_tables
6c3c93a826f4 report_path_data_table
feb5f01b5b52 add_analysis_info_files_table
```

Several migrations transform data, not only the schema (e.g.
`b1be74589382` builds checker sets from the old per-analysis checker table,
`6c3c93a826f4` moves bug path tables into `report_path_data`). A port that
must upgrade old databases has to re-implement each step from the migration
scripts; a port that only opens current databases needs the head schema
above and must refuse unknown revisions (`SCHEMA_MISMATCH_NO`).
