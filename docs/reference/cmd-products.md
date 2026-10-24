# Reference: products and `CodeChecker cmd products`

Usage: [../usage/cmd-products.md](../usage/cmd-products.md)

Python source: `codechecker_api/products.thrift`,
`web/client/codechecker_client/product_client.py`,
`web/client/codechecker_client/cli/cmd.py` (`__register_products`),
`web/server/codechecker_server/api/product_server.py`,
`web/server/codechecker_server/server.py` (`add_product`,
`is_database_used`, `get_product`, `remove_product`).

A product = one row of the config DB `products` table + one product
database ([server-database.md](server-database.md)). Its endpoint is the
first URL path element ([server-api.md](server-api.md) §2.2).

## 1. `cmd products list`

```
CodeChecker cmd products list [-o {plaintext,rows,table,csv,json}] [--url SERVER_URL]
```

Server-wide product client ([server-api.md](server-api.md) §6), logging to
stderr unless `table`. `getProducts(null, null, null, null, null)`.

- `json`: list of single-key objects `[{"<endpoint>": Product}, ...]`, the
  struct serialised field by field (enums as integers, base64 strings as
  is), one line.
- others: header `Database status, Endpoint, Name, Description`; status =
  `No access.` if `accessible` is false, else the message of
  `databaseStatus` ([server-database.md](server-database.md) §3.1;
  unknown → `Unknown database status`); name and description base64-decoded
  (empty if unset).

Exit 0.

## 2. `cmd products add`

```
CodeChecker cmd products add ENDPOINT [-n NAME] [--description TEXT]
    [--report-limit N] [--sqlite FILE | --postgresql]
    [--db-host/--dbaddress H] [--db-port/--dbport P] [--db-username/--dbusername U]
    [--db-password/--dbpassword PW] [--db-name/--dbname N] [--url SERVER_URL]
```

Defaults: `--sqlite <ENDPOINT>.sqlite`; PostgreSQL host `localhost`, port
`5432`, user `<ENDPOINT>`, password `""`, database `<ENDPOINT>`.

Client checks: any `--db-*` option without `--postgresql` (detected by
prefix-matching the raw argv) → `argument <opt>: not allowed without
argument --postgresql` (parser error, exit 1).

Request: `addProduct(ProductConfiguration{endpoint, displayedName_b64 =
b64(NAME) or null, description_b64 = b64(TEXT) or null, reportLimit = N or
null, connection = DatabaseConnection{engine "sqlite"|"postgresql", host,
port, username_b64, password_b64, database}})` (for SQLite: host `""`,
port 0, user/password b64 of `""`). True → INFO `Product added
successfully.`, exit 0; false → ERROR `Adding the product has failed.`,
exit 1; errors → exit 1.

Server `addProduct` (needs `SUPERUSER`), in order:

| Check | Error |
|---|---|
| endpoint valid ([server-api.md](server-api.md) §2.2 rule) | `GENERAL "The specified endpoint is invalid."` |
| connection given | `GENERAL "Product cannot be added without a database configuration!"` |
| PostgreSQL database name: non-empty, ≤ 63 bytes, no quotes, `;`, whitespace or control characters | `DATABASE "The specified PostgreSQL database name '<n>' contains characters that are not allowed ..."` |
| endpoint not configured yet | `GENERAL "A product endpoint '/<ep>' is already configured!"` |
| SQLite database is a bare file name (no directory part) | `DATABASE "SQLite database must be given by filename only!"` |
| database not used by another product or the config DB | `DATABASE "Database is already in use by another product!"` |

⚠️ The "in use" check compares resolved paths in a way that does not catch
an SQLite file already used by a product stored with a relative path:
`products add p4 --sqlite Default.sqlite` was accepted (verified), and both
products then share one database. A port should reject it.

Then:
1. PostgreSQL only: connect to the `postgres` database with the given
   credentials and run `CREATE DATABASE "<name>"`; "already exists" or
   other errors are logged and ignored (the connection below decides).
2. Display name = decoded name or the endpoint; description decoded or
   NULL.
3. Connection string: SQLite → `sqlite+pysqlite:///<file>` (relative to the
   workspace); PostgreSQL → as in [server-database.md](server-database.md)
   §2 with user default `codechecker` if the base64 user is empty, the
   password embedded in the URL.
4. Create the row (`run_limit` = `runLimit`, `report_limit` = `reportLimit`
   — the CLI sends null, which ends up as the default 500000 —,
   `is_review_status_change_disabled`, `confidentiality`), connect and
   create the schema. No connection → `DATABASE "Failed to add product."`
   (+ ` The SQLite database file must be under the server workspace
   directory.`); a connection error → `IOERROR "The configured connection
   for '/<ep>' failed: <error>"` (product removed again).
5. Insert, initialise product permissions, commit, reconnect, return true.

## 3. `cmd products del`

```
CodeChecker cmd products del ENDPOINT [--url SERVER_URL]
```

1. `getProducts(ENDPOINT, ...)` and keep products whose endpoint equals
   `ENDPOINT` exactly. None → ERROR `The product '<ep>' does not exist!`
   and **exit 0**.
2. `removeProduct(id)`: true → INFO `Product removed.`; false → ERROR `An
   error occurred in product removal.`, exit 1.

Server `removeProduct(id)` (needs `SUPERUSER`): unknown id →
`DATABASE "Product with ID <id> does not exist!"`; else disconnect and
delete the config DB row (cascades: product permissions and background
tasks of the product). The product database itself is not touched.

## 4. Other product API calls

| Call | Behavior |
|---|---|
| `getProducts(endpointFilter, nameFilter, limit, offset, sortMode)` | No permission needed. Filters: ILIKE on endpoint / display name with `\`, `%`, `_` escaped and `*` → `%` (so without `*` it is a case-insensitive exact match). If the server has more products in memory than the config DB rows, the extra ones are disconnected first. Sorting: `NAMES_ASC/DESC` (lower-case display name), `RUNS_ASC/DESC` (`num_of_runs`), `LATEST_STORE_ASC/DESC` (nulls last), default `ACCESSRIGHT`: if authentication is off, the caller is anonymous, `SUPERUSER` or has `PERMISSION_VIEW` → by lower-case display name; else accessible products first (an explicit product-view-granting grant to the user or a group, case-insensitive), then by name; product id as last key. `limit`/`offset` applied after ordering. |
| `getProductCount(endpointFilter, nameFilter)` | count with the same filters |
| `getCurrentProduct()` | only through a product route (else `IOERROR "Requested current product from ProductService but the request came through the main endpoint."`); product deleted meanwhile → `IOERROR "The product requested has been disconnected from the server."` |
| `getProductConfiguration(id)` | needs `PRODUCT_VIEW` or `PERMISSION_VIEW` on it; connection without password; SQLite path relative to the workspace |
| `editProduct(id, config)` | needs `PRODUCT_ADMIN`; changing endpoint or connection needs `SUPERUSER` and is tested with a temporary connection first; empty display name → the endpoint; sets run limit, report limit, review-status-change flag, confidentiality |
| `isAdministratorOfAnyProduct()` | `PRODUCT_ADMIN` on any product |

`Product` fields returned (`__get_product`): `id`, `endpoint`,
`displayedName_b64`, `description_b64` (null if none), `runCount` =
`num_of_runs`, `latestStoreToProduct` = `str(latest_storage_date)` or null,
`connected` = status `OK`, `accessible` = `PRODUCT_VIEW` or
`PERMISSION_VIEW`, `administrating` = `SUPERUSER` or `PRODUCT_ADMIN`,
`databaseStatus`, `admins` = names with a `PRODUCT_ADMIN` row,
`confidentiality` (NULL in the DB → `CONFIDENTIAL`), `reportLimit`.
`runLimit` and `runStoreInProgress` are not filled (null). A product row
without a live connection is connected on the fly (INFO `Product '<ep>'
was found in the configuration database but no database connection was
present. Mounting analysis run database...`).
