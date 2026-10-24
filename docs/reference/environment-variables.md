# Reference: environment variables

Usage: [../usage/environment-variables.md](../usage/environment-variables.md)

Every environment variable the 6.30.0 code reads (outside the test
suites), what reads it, and where the behavior is specified. Boolean
values are noted where the parsing differs.

## Launcher and package

| Variable | Read by | Meaning | Details |
|---|---|---|---|
| `CC_BIN_DIR` | entry point, `analyze` Makefile mode | package `bin/` (set by the launcher) | [common-infrastructure.md](common-infrastructure.md) §2 |
| `CC_LIB_DIR` | contexts | implementation directory (set if missing) | same |
| `CC_DATA_FILES_DIR` | contexts, logger, server | package root (`config/`, `www/`, `ld_logger/`) | same |
| `CODECHECKER_ORIGINAL_BUILD_ENV` | `log`, analyzers | file with the environment saved by the launcher; builds and analyzers run in it | same, [log.md](log.md) |
| `COLUMNS` | table output | terminal width | [common-infrastructure.md](common-infrastructure.md) §7 |
| `CC_LOG_CONFIG_PORT` | `server` (and other commands using the log config server) | start a Python log-config listener on this port | [common-infrastructure.md](common-infrastructure.md) §6 |

## Build logging (`log`, `ldlogger`)

`CC_LOGGER_GCC_LIKE`, `CC_LOGGER_JAVAC_LIKE`, `CC_LOGGER_FILE`,
`CC_LOGGER_KEEP_LINK`, `CC_LOGGER_DEF_DIRS`, `CC_LOGGER_ABS_PATH`,
`CC_LOGGER_DEBUG_FILE`, `CC_LOGGER_BIN`, `LD_PRELOAD`, `LD_LIBRARY_PATH`,
`PATH`, and for implicit include paths `CPATH`, `C_INCLUDE_PATH`,
`CPLUS_INCLUDE_PATH`: [log.md](log.md). (`config/config.json` also lists
the name `CODECHECKER_WORKSPACE`, which no code reads.)

## Analyzers

| Variable | Meaning | Details |
|---|---|---|
| `CC_ANALYZER_BIN` | `analyzer:path;analyzer2:path2`, exact binaries | [analyzers.md](analyzers.md) |
| `CC_ANALYZERS_FROM_PATH` | `yes`/`1` (case-insensitive): take analyzer binaries from `PATH` | [analyzers.md](analyzers.md), [analyzer-drivers.md](analyzer-drivers.md) |
| `CC_CLANGSA_PLUGIN_DIR` | Clang SA plugin directory when analyzers come from `PATH` | [analyzer-drivers.md](analyzer-drivers.md) |
| `CC_TEST_LABELS_DIR` | replaces `config/labels` (testing) | [checker-labels.md](checker-labels.md) |

## Output

| Variable | Read by | Details |
|---|---|---|
| `CC_REPO_DIR`, `CC_CHANGED_FILES`, `CC_REPORT_URL` | `parse -e gerrit`, `cmd diff -o gerrit` | [parse-export.md](parse-export.md) "Gerrit", [gerrit-integration.md](gerrit-integration.md) |

## Client (`cmd`, `store`)

| Variable | Meaning | Details |
|---|---|---|
| `CC_PASSWORD` | secret for `cmd login` only | [server-api.md](server-api.md) §7 |
| `CC_PASS_FILE` | password file (default `~/.codechecker.passwords.json`) | [server-api.md](server-api.md) §4.2 |
| `CC_SESSION_FILE` | session file (default `~/.codechecker.session.json`) | same |
| `CC_FORCE_SYNC_STORE` | `y`, `yes`, `t`, `true`, `on`, `1` (case-insensitive): use the legacy synchronous `massStoreRun` | [store.md](store.md) §3.3 |
| `http_proxy`, `https_proxy`, `no_proxy` (and upper-case forms) | proxies for API calls | [server-api.md](server-api.md) §3.5 |
| `SSL_CERT_FILE`, `SSL_CERT_DIR` | CA certificates for `https://` (OpenSSL) | same |
| `TEST_WORKSPACE` | enables `serverside-tasks --create-dummy-task` (testing) | [cmd-serverside-tasks.md](cmd-serverside-tasks.md) §3.4 |

## Server

| Variable | Meaning | Details |
|---|---|---|
| `CODECHECKER_DB_DRIVER` | PostgreSQL driver (`psycopg2` or `pg8000`) instead of auto-detection | [server-database.md](server-database.md) §2 |
| `PGPASSFILE` | libpq password file (pg8000: parsed by CodeChecker) | same |
| any `NAME` used as `$ENV:NAME$` in `server_config.json` | value substitution | [server.md](server.md) §7.1 |
| `TEST_WORKSPACE` | allows `createDummyTask` | [cmd-serverside-tasks.md](cmd-serverside-tasks.md) §3.4 |

## Files in the home directory

| File | Written by | Details |
|---|---|---|
| `~/.codechecker.session.json` | every client run (created empty: `{"tokens": {}}`) | [server-api.md](server-api.md) §4.2 |
| `~/.codechecker.passwords.json` | user (read only) | same |
| `~/.codechecker.instances.json` | `server` | [server.md](server.md) §3 |
| `~/.codechecker/` | default server workspace, default `store` input | [server.md](server.md), [store.md](store.md) |
