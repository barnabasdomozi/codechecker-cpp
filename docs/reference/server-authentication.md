# Reference: authentication, sessions, tokens and permissions

Usage: [../usage/server-authentication.md](../usage/server-authentication.md)

Python source: `web/server/codechecker_server/session_manager.py`,
`web/server/codechecker_server/api/authentication.py`,
`web/server/codechecker_server/permissions.py`,
`web/server/codechecker_server/auth/cc_ldap.py`,
`web/server/codechecker_server/auth/cc_pam.py`,
`web/codechecker_web/server/oauth_templates.py`,
`web/client/codechecker_client/client.py` (`login_user`),
`web/client/codechecker_client/token_client.py`,
`web/client/codechecker_client/permission_client.py`,
`web/client/codechecker_client/cli/cmd.py` (`__register_login`,
`__register_token`, `__register_permissions`).

Wire-level session handling (Bearer header, cookie, 401), the client's
session and password files and `cmd login` are in
[server-api.md](server-api.md) §4 and §7.

## 1. Configuration (`server_config.json` → `authentication`)

| Key | Template value | Meaning |
|---|---|---|
| `enabled` | `false` | authentication on/off (`--force-authentication` sets it to true) |
| `super_user` | `""` | user name that is always superuser ("root") |
| `realm_name`, `realm_error` | | texts for the web UI |
| `failed_auth_message` | `""` | appended to login failures as `\nNote: <text>` |
| `session_lifetime` | `300` | seconds a session lives after its last access |
| `refresh_time` | `60` | seconds after which an access updates `last_access` in the DB |
| `logins_until_cleanup` | `30` | logins between in-memory session clean-ups |
| `max_pers_auth_token_expiration_length` | `365` | max personal access token lifetime in days (code default 365) |
| `method_dictionary` | `enabled`, `auths`, `groups` | built-in users |
| `method_ldap` | `enabled`, `authorities[]` | §2.3 |
| `method_pam` | `enabled`, `users`, `groups` | §2.4 |
| `method_oauth` | `enabled`, `shared_variables`, `providers{}` | §2.5 |
| `regex_groups` | `enabled`, `groups{name: [regex]}` | §2.6 |

`session_lifetime`, `refresh_time` (may be absent) and
`logins_until_cleanup` are read without defaults: a config without
`session_lifetime` fails when sessions are used.

At start, if `enabled`:
- LDAP enabled but the LDAP library missing → WARNING `LDAP authentication
  was enabled but prerequisites are NOT installed on the system... Disabling
  LDAP authentication.` (same for PAM).
- OAuth enabled: apply templates (§2.5); no enabled provider → WARNING
  `OAuth authentication was enabled but no OAuth provider was enabled...
  Disabling OAuth authentication.`
- No usable method: with `--force-authentication` WARNING `Authentication
  was manually enabled, but no valid authentication backends are
  configured... The server will only allow the master superuser (root)
  access.` (authentication stays on); otherwise WARNING `Authentication is
  enabled but no valid authentication backends are configured... Falling
  back to no authentication.` and authentication is turned off.
- `soft_expire` (deprecated) is only noted in the debug log.

## 2. Logging in (`performLogin(authMethod, authString)`)

Empty `authString` → `RequestFailed(AUTH_DENIED, "No credentials supplied.
Refusing authentication!")`.

### 2.1 `Username:Password`

`authString` = `<user>:<secret>` (split at the first `:`).

`create_session(authString)`:
1. Authentication disabled → no session (the API returns the
   AUTH_DENIED "Invalid credentials" error below).
2. Count logins; every `logins_until_cleanup` logins, clean the in-memory
   session list: drop sessions whose refresh time has passed, and invalidate
   (also in the DB) sessions that are no longer alive.
3. **Existing session token**: if `auth_sessions` has a row with this
   `user_name` and `token = secret`, reuse that session (revalidate it) and
   return its token.
4. Otherwise try the methods in this order; the first success wins:
   personal access token (§3), dictionary (§2.2), PAM (§2.4), LDAP (§2.3).
5. Add `regex_groups` groups (§2.6).
6. Delete this user's expired `auth_sessions` rows.
7. New token = 32 random lowercase hex characters. Insert
   `auth_sessions(token, user_name, groups ';'-joined, last_access = now)`.
8. Return the token. INFO `'<user>' logged in.`

Failure → `RequestFailed(AUTH_DENIED, "Invalid credentials supplied for
user '<user>'. Refusing authentication!")` (+ `\nNote: <failed_auth_message>`
if set); WARNING with the same text on the server.

### 2.2 Dictionary

`auths` is a list of strings. The entry for a user is the first one whose
part before the first `:` equals the user name. Accepted if:
- the entry equals `authString` exactly (plain `user:password`), or
- the entry is `user:<hexdigest>:<algorithm>[:<salt>]`, `<algorithm>` is a
  hash name known to the platform (`sha256`, `md5`, ...), and
  `hexdigest(algorithm, password + salt)` equals `<hexdigest>`
  (lower-case hex, password and salt as UTF-8).

Groups: `groups[<user>]` (list) or none. On success the groups of all
personal access tokens of the user are updated to these groups.

### 2.3 LDAP

For each entry of `authorities` (first success wins):

1. `connection_url` required. Options: `referrals` (default false),
   `deref` (`never` or always), `tls_require_cert: "never"` disables
   certificate checking; LDAP v3.
2. User name escaped for LDAP filters; `accountPattern` with `$USN$`
   replaced. `accountBase` and `accountPattern` required.
   `accountScope`/`groupScope`: `base`, `one`, else subtree.
3. Bind as the service user (`username`/`password`), or with the user's own
   name and password if no service user is configured (a bind that succeeds
   but `whoami` returns nothing counts as invalid credentials).
4. Search the user DN under `accountBase`. Several results: the first whose
   DN ends with `user_dn_postfix_preference`, if configured; otherwise the
   first result.
5. Bind with the user DN and password. Success = authenticated.
6. Groups: `groupPattern` with `$USERDN$` replaced, searched under
   `groupBase`, collecting attribute `groupNameAttr` of each result (empty
   if `groupPattern`, `groupBase` or `groupNameAttr` is missing). The
   groups are written to the user's existing sessions and tokens.

### 2.4 PAM

Authenticate with the system PAM stack. If `users` and `groups` are both
empty, any authenticated user is accepted; else the user must be in `users`
or a member of one of `groups` (supplementary groups + primary group). No
groups are returned.

### 2.5 OAuth

Providers live in `method_oauth.providers.<name>`. Each enabled provider
must name a `template` (`github/v1`, `google/v1`, `ms_entra/v2.0`,
`gitlab/v1`; `default` or unknown templates disable the provider with a
WARNING). Template values (URLs, `scope`, `user_info_mapping`) are filled
in where the provider has none. Every string option except `enabled`,
`client_id`, `client_secret`, `template`, `variables`,
`user_info_mapping` is formatted with `{name}` placeholders from
`shared_variables` overridden by the provider's `variables`, plus
`provider` (a `host` still equal to `https://<server_host>` is removed;
an unknown placeholder disables the provider). `callback_url` (template
`{host}/login/OAuthLogin/{provider}`) must match
`^http(s|)://[a-zA-Z0-9._-]+([:][0-9]{2,5})?/login/OAuthLogin/<provider>$`
and the provider name must not contain `@`.

Flow:
1. `getOauthProviders()` → names of enabled providers.
2. `createLink(provider)`: provider still using `ExampleClientID` /
   `ExampleClientSecret` → disabled with an ERROR. Delete expired
   `oauth_sessions`, create a random `state` and a 48-character PKCE
   verifier, store `(state, verifier, provider, now + 15 min)` and return the
   provider's authorization URL (PKCE `S256`; Google also gets
   `access_type=offline&prompt=consent`). Errors →
   `AUTH_DENIED "createLink process failed."`
3. The browser returns to `<host>/login/OAuthLogin/<provider>?code=...&state=...`;
   the web UI calls `performLogin("oauth", "<provider>@<full callback URL>")`.
4. The server looks up the stored row by `state`; provider, state and
   expiry must match (`OAuth data mismatch.`); exchanges the code for a token
   (the token response must contain `access_token`, `refresh_token`,
   `token_type`, `scope`, `expires_in`); fetches user info; derives the user
   name by template and `user_info_mapping.username`:
   - GitHub: `email` → the primary e-mail from `user_emails_url` (skipped
     if `user_info_url` contains `localhost`); otherwise `login`;
   - Google: `email`;
   - Microsoft: `username` → claim `Signum` of the verified ID token
     (keys from `jwks_url`), else `mail`; groups = `displayName` of every
     entry of `user_groups_url` (following `@odata.nextLink`);
   - GitLab: `preferred_username`, `email` or `name` by mapping.
5. Create a session (token = 32 hex characters), store it and the OAuth
   tokens (`oauth_tokens`), update the user's personal access token groups,
   return the session token.

All OAuth failures are `RequestFailed(AUTH_DENIED, ...)`.

### 2.6 Regex groups

If `regex_groups.enabled`: for every group, if any of its regular
expressions matches anywhere in the user name (search, not full match),
the user is added to the group. Applied after any successful password or
token login.

### 2.7 Root (superuser) status

A session is "root" if its user is `super_user` or has a
`permissions_system` row `SUPERUSER` with that user **name** (groups do not
count here; they do count in normal permission checks, §5).

## 3. Sessions and personal access tokens

Session validity: `alive` ⇔ `now - last_access <= session_lifetime`.
When a request uses a session whose `refresh_time` has passed since
`last_access`, `last_access` is set to now (memory and DB). Sessions are
kept in each API process's memory and in `auth_sessions`; a token unknown
in memory is loaded from the DB. A token that is not alive is deleted from
both.

`destroySession()` deletes the caller's session (memory and DB); returns
true on success. INFO `'<user>' logged out.`

Personal access tokens (`personal_access_tokens`):
- As a login secret (§2.1 step 4): matched by `user_name` and `token`.
  Expired (`expiration < now`) → `RequestFailed(AUTH_DENIED, "Personal
  Access Token is expired!")`. Success → groups from the token row (split on
  `;`; an empty value gives one empty group name), `last_access` updated.
- `newPersonalAccessToken(name, description, expiration)` (needs a logged-in
  session: else `UNAUTHORIZED "The server must be start by using privilaged
  access to execute this action."`): `expiration` must be an integer in
  `1..max_pers_auth_token_expiration_length`, else `GENERAL "Invalid
  expiration length. The length must be between 1 and <max>!"`. The
  caller's session must exist in the DB (`DATABASE "Invalid session token.
  Make sure to login to the CodeChecker server first."`). Creates a new
  `auth_sessions` row (copy of the caller's user and groups) and a token row
  with a new 32-hex token, the trimmed name, the caller's groups,
  `creation_date = last_access = now`, `expiration = now + days`. Duplicate
  name for the user → `DATABASE "Personal access token with name '<n>'
  already exists."`. Returns the token with `lastAccess`, `expiration` as
  `YYYY-MM-DD HH:MM:SS.ffffff`.
- `getPersonalAccessTokens()`: the caller's tokens with `token` = `""`.
- `removePersonalAccessToken(name)`: not found → `DATABASE "Personal access
  token with name '<n>' was not found in the database for the user
  '<u>'."`.
- `getMaxTokenExpiration()` → the configured maximum.
- Deprecated `newToken`/`getTokens`/`removeToken` map onto the same table
  (random 8-letter name).

## 4. `CodeChecker cmd token`

All use the server URL ([server-api.md](server-api.md) §3.2) and the
authentication client with the stored token (§4.3 there if none).

| Command | Behavior |
|---|---|
| `token new NAME [--description D] [--expiration DAYS]` | `newPersonalAccessToken(NAME, D or null, DAYS or null)`; prints `The following access token has been generated for your account: <token>` and `The token is valid until: <expiration>`. ⚠️ Without `--expiration` the client sends no value and the server rejects it (`Invalid expiration length. The length must be between 1 and 365!`, exit 1), although the help suggests a default; a port should default to the maximum. |
| `token list [-o FORMAT]` | header `Name, Description, Last access, Expiration` (description empty if null), two-dimensional formatter; `-o json`: list of `{"token": "", "name", "description", "lastAccess", "expiration"}`. Logging to stderr unless `table`. |
| `token del NAME` | prints `'<NAME>' has been successfully removed.` (a false result prints `Error: '<NAME>' can not be removed.`). Not found → server error, exit 1. |

Errors go through the call wrapper ([server-api.md](server-api.md) §5),
exit 1.

## 5. Permissions

### 5.1 Model

| Permission | Scope | Inherited from (implies this one) | Managed by |
|---|---|---|---|
| `SUPERUSER` (1) | system | – | `SUPERUSER` |
| `PERMISSION_VIEW` (2) | system | `SUPERUSER` | `SUPERUSER` |
| `PRODUCT_ADMIN` (16) | product | `SUPERUSER` | `SUPERUSER`, `PRODUCT_ADMIN` |
| `PRODUCT_STORE` (18) | product | `SUPERUSER`, `PRODUCT_ADMIN` | `SUPERUSER`, `PRODUCT_ADMIN` |
| `PRODUCT_ACCESS` (17) | product | `SUPERUSER`, `PRODUCT_ADMIN`, `PRODUCT_STORE` | `SUPERUSER`, `PRODUCT_ADMIN` |
| `PRODUCT_VIEW` (19) | product | `SUPERUSER`, `PRODUCT_ACCESS`, `PRODUCT_ADMIN`, `PRODUCT_STORE` | `SUPERUSER`, `PRODUCT_ADMIN` |

Grants are rows `(permission, name, is_group)` in `permissions_system` or
`permissions_product` (with `product_id`).

`has_permission(P, session)` (direct):
- no session: true iff authentication is **disabled** (everyone has every
  permission on a server without authentication);
- root session and `P = SUPERUSER`: true;
- otherwise true iff a row grants `P` to the user name or to one of the
  session's groups. Names are compared **case-insensitively** (`lower()` on
  both sides). (The root check of §2.7 and `super_user` are exact,
  case-sensitive comparisons.)

Adding a grant: empty name → nothing; if the user/group already has rows
in that scope (case-insensitive match), the new row uses the stored
spelling of the name; if it already has this permission → nothing
(INFO `Permission '<P>' already added for ...`). Removing deletes the
case-insensitively matching row.

`require_permission(P)`: direct check, else breadth-first over the
"inherited from" lists (transitively). `require_manager(P)`: the caller has
(directly) at least one of P's managers.

The code also supports "default enabled" permissions (a `*` user row meaning
"everyone"), kept up by add/remove and created by `initialise_defaults` at
start and product creation. No permission is default-enabled in 6.30.0, so
`initialise_defaults` only removes stray `*` rows.

### 5.2 API

| Call | Behavior |
|---|---|
| `getAuthParameters()` | `{requiresAuthentication: auth enabled, sessionStillActive: caller has a live session}` |
| `getLoggedInUser()` | user name or `""` |
| `getAcceptedAuthMethods()` | always `["Username:Password", "oauth"]` |
| `getPermissions(scope)` | permissions of `SYSTEM` or `PRODUCT` |
| `getPermissionsForUser(scope, extraParams, filter)` | permissions of the scope for which (`given` → `has_permission`) and (`canManage` → `require_manager`) hold; no filter flags → same as `getPermissions` |
| `getAuthorisedNames(P, extraParams)` | needs manager rights (`UNAUTHORIZED "You can not manage the permission '<P>'"`); users and groups granted P directly (`*` removed) |
| `addPermission(P, name, isGroup, extraParams)` / `removePermission(...)` | needs manager rights; the name is trimmed on add; logs INFO `Permission '<P>' added for user|group '<n>' by '<caller>'.` |
| `hasPermission(P, extraParams)` | `require_permission` for the caller |
| `getAccessControl()` | needs `PERMISSION_VIEW` (`UNAUTHORIZED "You are not authorized to execute this action."`); all grants: `globalPermissions {user: {name: [perm names]}, group: {...}}` and `productPermissions {endpoint: {user, group}}`; the configured `super_user` gets `SUPERUSER` added |

`extraParams` is a JSON object string; product permissions need
`{"productID": <id>}`.

### 5.3 `CodeChecker cmd permissions`

```
CodeChecker cmd permissions [-o json] [--url SERVER_URL]
```

Only `json`. Logging to stderr. Calls `getAccessControl()` and prints one
line:

```json
{"version": 1,
 "global_permissions": {"user_permissions": {...}, "group_permissions": {...}},
 "product_permissions": {"<endpoint>": {"user_permissions": {...}, "group_permissions": {...}}}}
```

On a server without authentication the call is anonymous and allowed
(guests have every permission there).
