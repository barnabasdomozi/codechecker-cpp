# Reference: server API, HTTP layer and client connection

Shared by every page that talks to a server: [server.md](server.md),
[store.md](store.md), all `cmd-*.md` pages, [local-diff.md](local-diff.md)
(remote modes) and [web-ui.md](web-ui.md).

Python source: `codechecker_api/*.thrift` (the API definition),
`web/server/codechecker_server/server.py` (`RequestHandler`),
`web/server/codechecker_server/routing.py`,
`web/client/codechecker_client/product.py` (URL parsing),
`web/client/codechecker_client/client.py` (client setup, login),
`web/client/codechecker_client/credential_manager.py`,
`web/client/codechecker_client/thrift_call.py` (call wrapper and errors),
`web/client/codechecker_client/helpers/base.py`,
`web/codechecker_web/shared/env.py`, `web/codechecker_web/shared/version.py`,
`web/server/codechecker_server/api/server_info_handler.py`,
`web/server/codechecker_server/api/config_handler.py`.

## 1. The API

The API is defined in Apache Thrift IDL files. They are the normative
contract; a port must use the same files (same field ids, types and enum
values), because the web UI and older/newer clients depend on them.

| IDL file | Thrift service | URL service name | Scope |
|---|---|---|---|
| `shared.thrift` | – (types: `ErrorCode`, `RequestFailed`, `Permission`, `DBStatus`, `Ternary`, `TaskToken`) | | |
| `authentication.thrift` | `codeCheckerAuthentication` | `Authentication` | server-wide |
| `configuration.thrift` | `configurationService` | `Configuration` | server-wide |
| `server_info.thrift` | `serverInfoService` | `ServerInfo` | server-wide |
| `products.thrift` | `codeCheckerProductService` | `Products` | server-wide, or "seen from" one product |
| `tasks.thrift` | `codeCheckerServersideTaskService` | `Tasks` | server-wide |
| `report_server.thrift` | `codeCheckerDBAccess` | `CodeCheckerService` | one product |

API version: `<package>/lib/python3/codechecker_api/api_version.json`
→ `{"api_version": "6.74"}` (major 6, minor 74). The version is part of
every request URL.

### 1.1 Transport and protocol

- Every API call is one **HTTP POST** (HTTP/1.0 or 1.1; the reference server
  answers HTTP/1.0 and closes the connection). The client opens a new
  connection for every call.
- Request headers sent by the reference client: `Content-Type:
  application/x-thrift`, `Content-Length`, `User-Agent: Python/THttpClient
  (CodeChecker)`, optionally `Authorization: Bearer <token>` (§4).
- The body is one Thrift message in the **Thrift JSON protocol**
  (`TJSONProtocol`). The response is also one message, with
  `content-type: application/x-thrift` and `Content-Length`, HTTP status
  **200 even for errors**. The client does not look at the HTTP status.
- The server needs `Content-Length` to read the body.

Thrift JSON protocol, as needed for a port:

```
message  = [1, "<method name>", <type>, <seqid>, <struct>]
           type: 1 = CALL, 2 = REPLY, 3 = EXCEPTION, 4 = ONEWAY
struct   = {"<field id>": {"<type tag>": <value>}, ...}   unset fields omitted
type tag = "tf" (bool, 0/1) | "i8" | "i16" | "i32" | "i64" | "dbl" | "str"
           | "rec" (struct) | "map" | "lst" | "set"
list/set = ["<elem tag>", <count>, v1, v2, ...]
map      = ["<key tag>", "<value tag>", <count>, {"<key>": <value>, ...}]
           (keys are always JSON strings, also for integer keys)
binary   = base64 string under "str"
enum     = "i32"
```

The arguments of a call are a struct whose field ids are the parameter
numbers in the IDL. The reply struct has field `0` for the return value and
field `1` (or the declared id) for a declared exception.

Example (verified against the reference server):

```
POST /Default/v6.74/CodeCheckerService
[1,"getRunData",1,7,{"1":{"rec":{"2":{"lst":["str",1,"dem*"]},"3":{"tf":0}}},"2":{"i64":500}}]

[1,"getRunData",2,7,{"0":{"lst":["rec",1,{"1":{"i64":1},"2":{"str":"2026-10-24 11:40:35.335565"},
 "3":{"str":"demo"},"4":{"i64":0},"5":{"i64":3},"7":{"map":["i32","i32",1,{"0":5}]},
 "8":{"str":"v1"},"9":{"str":"6.30 (451b0c5c...)"},
 "10":{"map":["str","rec",2,{"clangsa":{"2":{"i64":0},"3":{"i64":3}},"clang-tidy":{...}}]},
 "11":{"str":"first"}}]}}]
```

### 1.2 Errors on the wire

Two kinds of error replies:

1. **Declared exception** `shared.RequestFailed {1: errorCode, 2: message,
   3: extraInfo list<string>}` — the normal way handlers report errors.
   `ErrorCode`: `DATABASE`=0, `IOERROR`=1, `GENERAL`=2, `AUTH_DENIED`=3,
   `UNAUTHORIZED`=4, `API_MISMATCH`=5 (6 and 7 are retired and must not be
   reused).
2. **`TApplicationException`** (message type 3) `{1: message, 2: type}` —
   the server sends type `6` (`INTERNAL_ERROR`) for every routing,
   authentication or unexpected error:

   ```
   [1,"getRunData",3,0,{"1":{"str":"Error code 401: Unauthorized!"},"2":{"i32":6}}]
   ```

   The method name and seqid are echoed from the request. Messages used:

   | Situation | Message |
   |---|---|
   | URL does not match §2.2, or unsupported API version | `Invalid request endpoint path.` |
   | Authentication required, no valid session (§4) | `Error code 401: Unauthorized!` |
   | Unknown product endpoint | `The product with the given endpoint '<ep>' does not exist!` |
   | `CodeCheckerService` without product | `Requested CodeCheckerService on a nonexistent product: 'None'.` |
   | Unknown service name | `No API endpoint named '<request path>'.` |
   | Product DB cannot be (re)connected | `'<ep>' database connection failed!` |
   | Product DB in another bad state | `'<ep>' database connection failed. DB status: <DBStatus name>` |
   | Any uncaught exception in a handler | `str(exception)` (also logged as WARNING with a traceback) |

   A dedicated message `The API version you are using is not supported by
   this server (server API version: 6.74)!` exists in the code but is
   unreachable: routing already rejects such paths (verified: `/v5.1/...`
   and `/v6.99/...` both return `Invalid request endpoint path.`).

## 2. Server: HTTP request handling

### 2.1 Methods

| Method | Behavior |
|---|---|
| `POST` | Thrift API call (§2.2). |
| `GET`, `HEAD` | Static web UI and probes (§2.3). |
| `OPTIONS` | Always `403`, no CORS headers (cross-site requests are not allowed). |

Every `GET`/`HEAD`/`POST` response carries these headers:

```
X-Frame-Options: DENY
X-XSS-Protection: 1; mode=block
X-Content-Type-Options: nosniff
Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline' 'self'; script-src 'unsafe-inline' 'self'; form-action 'self'; frame-ancestors 'none'
  (with TLS, '; upgrade-insecure-requests; block-all-mixed-content' is appended)
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Resource-Policy: same-origin
Referrer-Policy: no-referrer
```

Plus, on POST: `X-User: <user name>` when the request has a valid session;
otherwise, if the request sent any `Cookie` header,
`Set-Cookie: __ccPrivilegedAccessToken=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict`
(removes the deprecated session cookie from browsers).

### 2.2 POST routing

Path (query string ignored) split on `/` into at most 4 parts:

- `/<product>/v<major>.<minor>/<Service>` — product route. `<product>` must
  be a valid product endpoint: matches `^[A-Za-z0-9_\-]+$` and is **not** one
  of `index.html`, `images`, `docs`, `live`, `ready`, `Authentication`,
  `Products`, `CodeCheckerService`, `Tasks`.
- `/v<major>.<minor>/<Service>` — server-wide route (no product).

The version part has its leading `v` characters stripped and is split on
`.`; it is supported if it has ≥ 2 parts, `major == 6` and
`minor <= 74` (a third part is ignored). Anything else →
`Invalid request endpoint path.`

Then:

1. Read the method name from the message (for the log line
   `<client host>:<port> -- [<user or Anonymous>] POST <path>@<method>`,
   logged at INFO for every call).
2. Look up the session (§4). If authentication is enabled, the service is
   **not** `Authentication`, `Configuration` or `ServerInfo`, and there is no
   valid session → `Error code 401: Unauthorized!`.
3. If a product endpoint is present: find the product (in memory, else load
   it from the configuration database and connect); not found → error. If
   its database status is not `OK`: for `FAILED_TO_CONNECT`, `MISSING`,
   `SCHEMA_INIT_ERROR` try to reconnect once; other states (schema mismatch,
   …) → error (messages in §1.2).
4. Dispatch to the service handler. `CodeCheckerService` requires a product.
   `Products` works with or without one (with a product, `getCurrentProduct`
   returns it). Other services ignore the product part.
5. Unexpected exceptions become a `TApplicationException` (§1.2). A client
   closing the connection (broken pipe) is logged as WARNING and ignored.

### 2.3 GET handling

Files are served from the package's `www/` directory.

1. `/` → `index.html`.
2. `/live` → `200`, body `CODECHECKER_SERVER_IS_LIVE`.
3. `/ready` → runs a query on the configuration database: `200`
   `CODECHECKER_SERVER_IS_READY`, or `500` `CODECHECKER_SERVER_IS_NOT_READY`.
4. If the first path element is a valid product endpoint (§2.2 rule), remove
   `<product>/` once from the path (`/Default/runs` → `/runs`).
5. If the remaining path is `/` or does not name an existing file under
   `www/` → serve `index.html` (single-page-app fallback). Otherwise serve the
   file. Directory listings → `405 No permission to list directory`.

GET requests are never authenticated by the server; the web UI performs
login through the API.

### 2.4 `ServerInfo` and `Configuration` services

- `ServerInfo.getPackageVersion()` → the server's package version string,
  which is the **git describe tag** of the build (`6.30`), or the "dirty"
  describe string (`6.30-85-g451b0c5c1`) when the server runs with
  `--verbose debug` or `debug_analyzer`. (Same value from the deprecated
  `Products.getPackageVersion()`.)
- `Configuration.getNotificationBannerText()` → base64 of the
  `server_configurations` row `notification_banner_text` (empty string if
  none). No permission needed.
- `Configuration.setNotificationBannerText(b64)` → stores the decoded text.
  With authentication enabled it needs a session whose user is the
  configured `super_user` or holds `SUPERUSER`
  (`You are not authorized to modify the notification.`, `UNAUTHORIZED`);
  without authentication anyone may set it.

## 3. Client: URLs

### 3.1 Product URL (`--url PRODUCT_URL`, default `localhost:8001/Default`)

`split_product_url(url)`:

1. If `://` is in the URL: protocol = text before it; the rest has leading
   and trailing `/` stripped. Protocol must be `http` (default port 80) or
   `https` (443), else error. Without protocol: `http`, port 8001.
2. Split the rest on `/`:
   - 1 part: it is the **product name** on `localhost`. Error if it starts
     with a digit or contains `.` or `:` (so `localhost:8555` alone is an
     error, but `https://h` means product `h` on `localhost:443`).
   - 2 parts: `host[:port]/product`. Host part per §3.3.
   - more: error (`Product URL can not contain extra '/' chars.`).

Any error → `ERROR Malformed product URL was provided. A valid product URL
looks like this: 'http://my.server.com:80/ProductName'.`, **exit 2**.

| Input | protocol, host, port, product |
|---|---|
| `Default` | `http, localhost, 8001, Default` |
| `host/P` | `http, host, 8001, P` |
| `http://h:9/P/` | `http, h, 9, P` |
| `https://h/P` | `https, h, 443, P` |
| `[::1]:9/P` | `http, [::1], 9, P` |
| `h:9`, `a/b/c`, `1prod`, `ftp://h:9/P` | error |

### 3.2 Server URL (`--url SERVER_URL`, default `localhost:8001`)

Used by `cmd login`, `token`, `permissions`, `products`, `serverside-tasks`.
Same protocol handling; then only the part before the first `/` is used
(`h:9/Prod/x` → `h:9`). Errors here are **not** caught: an unknown protocol
or a non-numeric port prints a Python traceback (`ValueError`) and exits 1.

### 3.3 Host and port

`host:port` → split on `:`:
- 2 parts → host, integer port;
- 1 part → host, default port;
- more (IPv6): if the first part starts with `[` and the last ends with `]`
  → whole string is the host (`[::1]`); if the second-to-last ends with `]`
  → host = all but the last part, port = last part; otherwise error.

The brackets stay in the host string. The request URL is
`<protocol>://<host>:<port><path>`.

### 3.4 Request paths used by the client

| Client | Path |
|---|---|
| results (`cmd`, `store`) | `/<product>/v6.74/CodeCheckerService` |
| authentication | `/v6.74/Authentication` |
| configuration | `/v6.74/Configuration` |
| products, server-wide | `/v6.74/Products` |
| products, for one product | `/<product>/v6.74/Products` |
| tasks | `/v6.74/Tasks` |

The client always sends its own API version. It never calls
`checkAPIVersion`; an incompatible server shows up as an
`Invalid request endpoint path.` error (§5).

### 3.5 Proxy and TLS

- Proxies come from the standard environment (`http_proxy`/`HTTP_PROXY`,
  `https_proxy`/`HTTPS_PROXY`, `no_proxy`), chosen by the URL scheme. HTTPS
  through a proxy uses `CONNECT`; HTTP sends the absolute URL. User info in
  the proxy URL becomes `Proxy-Authorization: Basic ...`.
- If a proxy is used but its URL has no host or no integer port (e.g. no
  scheme), the client exits 1 with
  `Invalid proxy format! Check your HTTP_PROXY/HTTPS_PROXY environment variables if these are in the right format: 'http[s]://host:port'.`
- HTTPS verifies the certificate against the system trust store, which
  honours `SSL_CERT_FILE` / `SSL_CERT_DIR` (OpenSSL variables), and checks
  the host name. `REQUESTS_CA_BUNDLE` has no effect.

## 4. Sessions and credentials

### 4.1 On the wire

- The client sends `Authorization: Bearer <token>` on every call once it has
  a token.
- The server also accepts the deprecated cookie
  `__ccPrivilegedAccessToken=<token>` (only when no valid Bearer token was
  found; the `Cookie` header is split on `"; "`, each item on `=`).
- A token is valid if the session exists (memory or the `auth_sessions`
  table) and `now - last_access <= session_lifetime` (see
  [server-authentication.md](server-authentication.md)). Without
  authentication enabled, tokens are ignored and every caller is anonymous.

### 4.2 Client files

| File | Default | Override | Content |
|---|---|---|---|
| Session file | `~/.codechecker.session.json` | `CC_SESSION_FILE` | `{"tokens": {"host:port": "<token>"}}` |
| Password file | `~/.codechecker.passwords.json` | `CC_PASS_FILE` | `{"client_autologin": bool, "credentials": {"<key>": "user:secret"}}` |

Loading (`UserCredentials`, at every client setup):

1. Log INFO `Checking local passwords or tokens in <password file>`.
2. Password file exists: warn if group/other can read or write it
   (`'<file>' is readable by users other than you! ...`), load JSON. A file
   that is not valid JSON, or has no `credentials` key, crashes with a
   `KeyError: 'credentials'` traceback (exit 1); a port should report an
   error instead. Keys of `credentials` are normalised with
   `^(http[s]?://)*(?P<host>[^:/\s]+):*(?P<port>\d+)*/*(\w+)*$` to `host:port`
   or `host` (protocol and product removed); non-matching keys are dropped
   with WARNING `Not a valid host entry: <key>.` If empty: INFO
   `No saved tokens.`
   Otherwise, if `~/.codechecker.password.json` (singular) exists: WARNING
   `Typo in file name! Rename '<that>' to '<password file>'.`; else INFO
   `Password file not found.`
3. `client_autologin` defaults to **true**.
4. Log INFO `Checking for local valid sessions.` Open the session file
   (created if missing) under an exclusive file lock; if it is not valid
   JSON, write `{"tokens": {}}` and chmod 0600. Warn about permissions as
   above.

Credential lookup for `host`, `port`: first match of `host:port`, `host`,
`*:port`, `*`. The secret after the first `:` is a password or a personal
access token.

Saving a token rewrites the whole session file (exclusive lock, JSON with
2-space indent, sorted keys). The key is `host:port` exactly as parsed from
the URL (so `localhost` and `127.0.0.1` are different entries).

### 4.3 Getting a token (every command that connects)

`get_new_token` / `perform_auth_for_handler`, used when the session file has
no token for `host:port`:

1. Call `Authentication.getAuthParameters()` (a `TApplicationException`
   counts as "no authentication").
2. If `requiresAuthentication` and not `sessionStillActive`:
   - if autologin is enabled and a credential is found (§4.2): check the user
     part is non-empty (else ERROR `No username supplied! Please specify the
     username in your "<password file>" file for <host>:<port>.`, exit 1),
     INFO `Logging in using pre-configured credentials...`, call
     `performLogin("Username:Password", "user:secret")`; on success save the
     token, INFO `Authentication successful.` and use it;
   - otherwise, or if login failed: with autologin enabled ERROR
     `Invalid pre-configured credentials.` + ERROR `Your password has been
     changed or personal access token has been removed which is used by your
     "<password file>" file. Please remove or change invalid credentials.`
     (printed even when no password file exists); with autologin disabled
     ERROR `Access denied. This server requires authentication.` + ERROR
     `Please log in onto the server using 'CodeChecker cmd login'.`. Exit 1.
3. Otherwise no token (anonymous).

Products and tasks clients log INFO `No valid token or session was found for
<host>:<port>` before this step (it shows up on servers without
authentication, e.g. in `cmd permissions`).

### 4.4 Expired tokens

Every API call goes through a wrapper (§5). If the call fails with a
`TApplicationException` whose message contains `Error code 401`, the client
obtains a new token (§4.3; this may log in again from the password file),
sets the new `Authorization` header and repeats the call **once**.

## 5. Client call wrapper and exit codes

Every API method of the client helpers is wrapped:

1. Open the transport, call, close the transport (always).
2. 401 retry (§4.4).
3. `RequestFailed`:
   - log ERROR `Calling API endpoint: <method>`;
   - `GENERAL` with `extraInfo[0] == "report_limit"` is re-raised to the
     caller (used by `store`, [store.md](store.md));
   - `DATABASE` → ERROR `Database error on server\n<message>`;
   - `AUTH_DENIED` → ERROR `Authentication denied\n <message>`;
   - `UNAUTHORIZED` → ERROR `Unauthorized to access\n <message>` and ERROR
     `Ask the product admin for additional access rights.`;
   - `API_MISMATCH` → ERROR `Client/server API mismatch\n <message>`;
   - other → ERROR `Error during API call: <method>`, ERROR `<message>`,
     and each `extraInfo` line as ERROR;
   - exit 1.
4. `TApplicationException` → ERROR `Internal server error: <message>`, exit 1.
5. Thrift protocol error → ERROR naming the kind (`Unknown thrift error`,
   `Thrift invalid data error.`, `... negative size ...`, `... size limit
   ...`, `... bad version ...`), the method name, the argument list with
   strings longer than 100 characters cut to 100 + `...` (omitted for
   `performLogin`), the keyword arguments, a traceback; exit 1.
6. OS / socket / TLS error → ERROR `Connection failed.`, ERROR
   `OS Error: <error>`, ERROR `Check if your CodeChecker server is running.`,
   exit 1.

## 6. Client setup per command kind

- **Results client** (`cmd runs/results/diff/sum/...`, `store`): parse the
  product URL (§3.1); token from the session file or §4.3; endpoint
  `/<product>/v6.74/CodeCheckerService`. No call is made at setup.
- **Product client for a product** (`store`, `cmd components`, ...): as
  above, then `Products.getPackageVersion()` and
  `Products.getCurrentProduct()` through `/<product>/v6.74/Products`; if
  either returns empty → ERROR `The product '<p>' cannot be communicated
  with. It either doesn't exist, or the server's configuration is bogus.`,
  exit 1.
- **Server-wide clients** (`login`, `token`, `permissions`, `products`,
  `serverside-tasks`): parse the server URL (§3.2), token from the session
  file or §4.3.

## 7. `CodeChecker cmd login`

```
CodeChecker cmd login [USERNAME] [-d/--deactivate/--logout] [--url SERVER_URL]
```

`USERNAME` defaults to the OS user name.

1. Parse the server URL. Call `Configuration.getNotificationBannerText()`
   (anonymous); if non-empty, INFO `Announcement: <decoded text>`.
2. **Logout** (`-d`): set up the authentication client with the stored
   token (§4.3 if there is none), call `destroySession()`; if it returns
   true, remove `host:port` from the session file and INFO
   `Successfully logged out.` Exit 0.
3. **Login**:
   1. `getAuthParameters()`; a `TApplicationException` → INFO `This server
      does not support privileged access.`, exit 0. Not
      `requiresAuthentication` → INFO `This server does not require
      privileged access.`, exit 0.
   2. `getAcceptedAuthMethods()` must contain `Username:Password` (the
      reference server always returns `["Username:Password", "oauth"]`),
      else CRITICAL `No authentication methods were reported by the server
      that this client could support.`, exit 1.
   3. Password source, in order:
      - `CC_PASSWORD` set: INFO `Using credentials from CC_PASSWORD env var
        to log in.`; it is the secret for `USERNAME` (if `USERNAME` is
        empty: ERROR `Cannot login. CC_PASSWORD env var is defined,but
        username is not provided!`, exit 1);
      - else, if autologin is enabled: the password-file credential (§4.2);
      - if a credential was found and its user part equals `USERNAME`: INFO
        `Logging in using preconfigured credentials...` and use its secret
        (an empty user part → the `No username supplied!` error, exit 1);
      - otherwise INFO `Logging in using credentials from command line...`
        and prompt `Please provide password for user '<USERNAME>': ` on the
        terminal (no echo; when stdin is not a terminal the line is read
        from stdin with the warning `Password input may be echoed.`).
   4. `performLogin("Username:Password", "<USERNAME>:<secret>")` → save the
      token, INFO `Server reported successful authentication.`, exit 0. A
      `RequestFailed` → handled by the wrapper (§5: `Calling API endpoint:
      performLogin`, `Authentication denied\n Invalid credentials supplied
      for user '<u>'. Refusing authentication!`), exit 1.

The secret is split from the user at the **first** `:` by the server, so
passwords may contain `:`; the client's own split of a password-file entry
uses `split(":")[1]` and would cut a password at its second `:`.

## 8. Shared API details referenced from other pages

### 8.1 `getAnalysisStatistics(runId, runHistoryId)` → `map<analyzer, AnalyzerStatistics>`

Needs view permission. Rows of `analyzer_statistics`
([server-database.md](server-database.md) §4.2):
- `runId` given: only rows of the run's **newest** history entry that has
  statistics (`MAX(run_history_id)` among the run's rows);
- else `runHistoryId` given: rows of that history entry;
- else all rows (later rows overwrite earlier ones per analyzer).

`AnalyzerStatistics = {version: zlib-decoded text or null, failed,
successful, failedFilePaths: zlib-decoded text split on "\n" (or [])}`,
keyed by `analyzer_type`.

### 8.2 `ReportFilter.annotations` (web UI only)

A list of `Pair{first: key, second: value}`. Values are grouped per key.
For each key: a report matches if it has an annotation with that key and
a value ILIKE any of the key's values (`*` → `%`); a key with no values
matches any report that has the key. Keys are OR-ed (a report matching
any key passes). In `getRunResults` the same condition is applied as a
`HAVING` on the pivoted `annotation_<key>` columns (OR over all key/value
pairs). Allowed keys are those of [store.md](store.md) §4.6.
