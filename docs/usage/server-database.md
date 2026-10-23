# Server database status and schema upgrade

When a new CodeChecker version changes the database schema, the existing
databases must be upgraded (migrated with Alembic). These options are given to
`CodeChecker server`, with the **same** workspace and database arguments that
are used to start the server:

```
--db-status PRODUCT|all
--db-upgrade-schema PRODUCT|all
--db-force-upgrade
```

The three options are mutually exclusive. Back up the databases before
upgrading.

## Automatic check at start ✅

On every start, the server checks the configuration database and every
product database, and prints a status table:

```
[INFO] - Checking configuration database ...
[INFO] - Database is up to date.
...
[INFO] - Status of products:
┌──────────────────┬─────────────────────────┬─────────────────────┬────────────────────────────────┬───────────────────────────────┐
│ Product endpoint │ Database status         │ Database location   │ Schema version in the database │ Schema version in the package │
├──────────────────┼─────────────────────────┼─────────────────────┼────────────────────────────────┼───────────────────────────────┤
│ Default          │ Database is up to date. │ ./ws/Default.sqlite │ feb5f01b5b52 (up to date)      │ feb5f01b5b52                  │
│ proj2            │ Database is up to date. │ ./ws/proj2.sqlite   │ feb5f01b5b52 (up to date)      │ feb5f01b5b52                  │
└──────────────────┴─────────────────────────┴─────────────────────┴────────────────────────────────┴───────────────────────────────┘
```

If the configuration database or any product database is older and can be
upgraded, the server warns ("Multiple products can be upgraded, make a
backup!") and asks `Do you want to upgrade to the new schema? Y(es)/n(o)`.
With `--db-force-upgrade`, it upgrades without asking. This startup upgrade
path is the one that works in 6.30.0 (from the source code; the test
databases were already up to date).

## Show the database status ⚠️

```sh
CodeChecker server -w ./ws --db-status all
CodeChecker server -w ./ws --db-status Default
```

**Intended:** print the status table above for all products (or one) and
exit.

**Actual in 6.30.0:** the option is ignored and a normal server starts on the
default port 8001. It keeps running until stopped. The debug log shows:

```
[DEBUG] - server.py:963 server_init_start() - Status was not in the arguments.
...
[INFO] - Server waiting for client requests on [localhost:8001]
```

Cause: `server_init_start()` in
`web/server/codechecker_server/cli/server.py` passes `context.migration_root`,
but the context object has only `run_migration_root` and
`config_migration_root`. The `AttributeError` is caught by an
`except AttributeError` that is meant for a missing `args.status`, so the
code continues and starts the server.

## Upgrade product databases ⚠️

```sh
CodeChecker server -w ./ws --db-upgrade-schema all
CodeChecker server -w ./ws --db-upgrade-schema Default
CodeChecker server -w ./ws --db-force-upgrade
```

**Intended:** upgrade one or all product databases (asking for confirmation
unless `--db-force-upgrade`) and exit.

**Actual in 6.30.0:** same bug as `--db-status`. The option is ignored and a
server starts (log: `Product upgrade was not in the arguments.`). Outdated
databases are still upgraded by the normal startup check described above.
`--db-force-upgrade` starts the server and upgrades without asking.

⚠️ The help text implies `--db-upgrade-schema all --db-force-upgrade` can be
combined. They are in one mutually exclusive group, so this fails:

```
CodeChecker server: error: argument --db-force-upgrade: not allowed with argument --db-upgrade-schema
```

## For the port

The C++ version should implement the intended behavior: print the status, or
upgrade, and exit with the status code. See [known-issues.md](known-issues.md).
