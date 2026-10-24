# Manage products (`cmd products`)

A product is a separate result database with its own URL endpoint
(`host:port/ENDPOINT`). These commands take a **server** URL (`host:port`),
not a product URL. Adding and removing products needs `SUPERUSER` permission
on an authenticated server.

## List products ✅

```sh
CodeChecker cmd products list --url localhost:8555
CodeChecker cmd products list -o json --url localhost:8555
```

```
┌─────────────────────────┬──────────┬─────────────┬──────────────────────────────────────────┐
│ Database status         │ Endpoint │ Name        │ Description                              │
├─────────────────────────┼──────────┼─────────────┼──────────────────────────────────────────┤
│ Database is up to date. │ Default  │ Default     │ Default product created at server start. │
│ Database is up to date. │ proj3    │ proj3       │                                          │
│ Database is up to date. │ proj2    │ Project Two │ second product                           │
└─────────────────────────┴──────────┴─────────────┴──────────────────────────────────────────┘
```

## Add a product (SQLite) ✅

```sh
CodeChecker cmd products add proj2 -n "Project Two" --description "second product" \
  --report-limit 1000 --url localhost:8555
CodeChecker cmd products add proj3 --sqlite proj3.sqlite --url localhost:8555
```

```
[INFO] - Product added successfully.
```

- `ENDPOINT` (first argument): URL part; letters, digits, `_` and `-`.
- `-n`: display name (defaults to the endpoint).
- `--report-limit`: maximum number of reports one store may have; larger
  stores are rejected.
- `--sqlite`: database file, relative to the server's workspace (default
  `<ENDPOINT>.sqlite`).

Store into the new product with `--url localhost:8555/proj2`.

## Add a product (PostgreSQL) ✅

```sh
CodeChecker cmd products add proj4 --postgresql --db-host localhost --db-port 5432 \
  --db-username cc --db-password secret --db-name proj4 --url localhost:8555
```

The database must already exist, and the **server** connects to it. Defaults:
username and database name = `<ENDPOINT>`. Verified with a temporary
PostgreSQL 17 server: the product works like a SQLite one (`store`,
`cmd results`), and the reports are in the PostgreSQL database. If the
database does not exist:

```
[ERROR] - Calling API endpoint: addProduct
[ERROR] - Database error on server
```

(exit code 1). The password given with `--db-password` is stored in the
product's connection string in the configuration database.

## Remove a product ✅

```sh
CodeChecker cmd products del proj3 --url localhost:8555
```

```
[INFO] - Product removed.
```

This only disconnects the database; the database file and its data stay.
The product's permission settings are lost.
