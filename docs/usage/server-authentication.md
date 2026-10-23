# Authentication, sessions, tokens and permissions

By default a server needs no login and everyone has every permission. With
authentication enabled, clients must log in. Permissions then control what
each user can do.

## Turn on authentication ✅

Edit `server_config.json` in the server's config directory (by default the
workspace). This example uses the built-in user list ("dictionary"):

```json
"authentication": {
  "enabled": true,
  "super_user": "admin",
  "realm_name": "CodeChecker Privileged server",
  "realm_error": "Access requires valid credentials.",
  "session_lifetime": 300,
  "refresh_time": 60,
  "logins_until_cleanup": 30,
  "max_pers_auth_token_expiration_length": 365,
  "method_dictionary": {
    "enabled": true,
    "auths": ["admin:admin123", "bob:bob123"],
    "groups": {"bob": ["devs"]}
  },
  "method_ldap":  {"enabled": false, ...},
  "method_pam":   {"enabled": false, ...},
  "method_oauth": {"enabled": false, ...},
  "regex_groups": {"enabled": false, "groups": {...}}
}
```

- `super_user` gets the `SUPERUSER` permission.
- `auths` entries are `user:password`. `groups` maps users to groups for
  permissions.
- LDAP, PAM and OAuth are other login methods. ❌ Not tested (no LDAP/PAM/OAuth
  server available).
- `regex_groups` puts users into groups by regex on the user name.
- Keep the file `chmod 0600`; the server warns otherwise.

Start the server (or use `CodeChecker server -r` to reload a running one):

```sh
CodeChecker server -w ./ws3 -v 8560
```

Unauthenticated calls now fail:

```sh
CodeChecker cmd runs --url localhost:8560/Default
```

```
[ERROR] - Invalid pre-configured credentials.
[ERROR] - Your password has been changed or personal access token has been removed which is used by your "~/.codechecker.passwords.json" file. Please remove or change invalid credentials.
```

(exit code 1). ⚠️ This message is printed even when the password file does
not exist.

### Force authentication from the command line ✅

```sh
CodeChecker server -w ./ws -v 8561 --force-authentication
```

```
[INFO] - '--force-authentication' was passed as a command-line option. The server will ask for users to authenticate!
[WARNING] - Authentication was manually enabled, but no valid authentication backends are configured... The server will only allow the master superuser (root) access.
```

## Log in ✅

```sh
CodeChecker cmd login admin --url localhost:8560          # asks for the password
```

```
Please provide password for user 'admin':
[INFO] - Server reported successful authentication.
```

The user name defaults to the current OS user. The password can also come from
standard input or from `CC_PASSWORD`:

```sh
echo admin123 | CodeChecker cmd login admin --url localhost:8560     # "Password input may be echoed."
CC_PASSWORD=admin123 CodeChecker cmd login admin --url localhost:8560
```

```
[INFO] - Using credentials from CC_PASSWORD env var to log in.
[INFO] - Logging in using preconfigured credentials...
[INFO] - Server reported successful authentication.
```

The session is saved in `~/.codechecker.session.json` (or the file named by
`CC_SESSION_FILE`). All later `cmd` and `store` calls to this server use it.

Wrong password:

```
[ERROR] - Calling API endpoint: performLogin
[ERROR] - Authentication denied
 Invalid credentials supplied for user 'bob'. Refusing authentication!
```
(exit code 1)

## Log out ✅

```sh
CodeChecker cmd login -d --url localhost:8560      # also --deactivate, --logout
```

```
[INFO] - Successfully logged out.
```

## Automatic login with a password file ✅

`~/.codechecker.passwords.json` (or the file named by `CC_PASS_FILE`):

```json
{
  "client_autologin": true,
  "credentials": {
    "localhost:8560": "admin:11bc1366abca8fa1632f965b72393638"
  }
}
```

```sh
CodeChecker cmd runs --url localhost:8560/Default -o csv
```

```
[INFO] - Authentication successful.
Name,Number of unresolved reports,...
authrun,3,...
```

Credential keys are matched in this order: exact `host:port`, `host`,
`*:port`, and `*` for all servers. The value is `user:password` or
`user:personal-access-token`. Keep the file `chmod 0600`.

## Personal access tokens ✅

A token can be used instead of the password (in `CC_PASSWORD` or the password
file), so passwords are not stored in CI. You must be logged in to manage
tokens.

```sh
CodeChecker cmd token new ci_token --description "CI token" --expiration 30 --url localhost:8560
```

```
The following access token has been generated for your account: 11bc1366abca8fa1632f965b72393638
The token is valid until: 2026-11-22 23:48:50.774769
```

`--expiration` is in days, 1 to 365 (the server maximum is
`max_pers_auth_token_expiration_length`).

```sh
CodeChecker cmd token list --url localhost:8560
CodeChecker cmd token list -o json --url localhost:8560
```

```
┌──────────┬─────────────┬────────────────────────────┬────────────────────────────┐
│ Name     │ Description │ Last access                │ Expiration                 │
├──────────┼─────────────┼────────────────────────────┼────────────────────────────┤
│ ci_token │ CI token    │ 2026-10-23 23:48:50.774769 │ 2026-11-22 23:48:50.774769 │
└──────────┴─────────────┴────────────────────────────┴────────────────────────────┘
```

```json
[{"token": "", "name": "ci_token", "description": "CI token",
  "lastAccess": "2026-10-23 23:48:50.774769", "expiration": "2026-11-22 23:48:50.774769"}]
```

The token value itself is not shown again.

Log in with a token:

```sh
CC_PASSWORD=11bc1366abca8fa1632f965b72393638 CodeChecker cmd login admin --url localhost:8560
```

Delete a token:

```sh
CodeChecker cmd token del ci_token --url localhost:8560
```

```
'ci_token' has been successfully removed.
```

After deletion, logging in with the token fails with "Invalid credentials".

## Permissions ✅

```sh
CodeChecker cmd permissions --url localhost:8560
```

```json
{"version": 1,
 "global_permissions": {"user_permissions": {"admin": ["SUPERUSER"]}, "group_permissions": {}},
 "product_permissions": {}}
```

`-o json` is the only output format. This needs the `PERMISSION_VIEW`
permission.

Permissions: server-wide `SUPERUSER` and `PERMISSION_VIEW`; per product
`PRODUCT_ADMIN`, `PRODUCT_STORE`, `PRODUCT_ACCESS`, `PRODUCT_VIEW`. They are
granted in the web UI. A user without rights gets:

```sh
CC_PASSWORD=bob123 CodeChecker cmd login bob --url localhost:8560
CodeChecker cmd products add bobprod --url localhost:8560
```

```
[ERROR] - Unauthorized to access
 You are not authorized to execute this action.
[ERROR] - Ask the product admin for additional access rights.
```
(exit code 1)

On a server without authentication, `cmd permissions` prints empty
permission lists and a note: `No valid token or session was found for localhost:8555`.
