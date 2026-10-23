# Environment variables

| Variable | Used by | Meaning | Tested |
|---|---|---|---|
| `CC_LOGGER_GCC_LIKE` | `log`, `ldlogger` | Colon-separated list of compiler names to record. Default `gcc:g++:clang:clang++:/cc:c++` | ✅ |
| `CC_LOGGER_FILE` | `ldlogger` (direct use) | Output compilation database | ✅ |
| `CC_LOGGER_KEEP_LINK` | `log` | `true`: keep link-only commands (same as `log -k`) | ✅ |
| `CC_LOGGER_DEF_DIRS` | `log` | Add the compiler's implicit include folders to each command | ✅ |
| `CC_LOGGER_ABS_PATH` | `log` | Make include folder arguments absolute | ✅ |
| `CC_LOGGER_DEBUG_FILE` | `log --verbose debug` | Where to write the logger debug log | ✅ |
| `CC_ANALYZERS_FROM_PATH` | `analyzers`, `checkers`, `analyze`, `check` | `yes`/`1`: take analyzer binaries from `PATH` | ✅ |
| `CC_ANALYZER_BIN` | same | `analyzer:/path;analyzer2:/path2` exact binaries | ✅ |
| `CC_CLANGSA_PLUGIN_DIR` | same | Clang SA plugin folder when `CC_ANALYZERS_FROM_PATH` is set | ❌ |
| `CC_REPO_DIR` | `parse -e gerrit`, `cmd diff -o gerrit` | Repository root; paths are made relative to it | ✅ |
| `CC_CHANGED_FILES` | same | Gerrit changed-files JSON (required for gerrit output) | ✅ |
| `CC_REPORT_URL` | same | Link added to the review message | ✅ |
| `CC_PASSWORD` | `cmd login`, all clients | Password or personal access token | ✅ |
| `CC_PASS_FILE` | all clients | Password file (default `~/.codechecker.passwords.json`) | ✅ |
| `CC_SESSION_FILE` | all clients | Session file (default `~/.codechecker.session.json`) | ✅ |
| `SSL_CERT_FILE` | all clients | CA certificate to trust for `https://` servers | ✅ |
| `PGPASSFILE` | server (PostgreSQL) | libpq password file | ❌ |
| `COLUMNS` | all table output | Table width | ✅ |

Files CodeChecker creates in the home folder:

| File | Created by |
|---|---|
| `~/.codechecker.session.json` | Any client call to a server (`{"tokens": {}}` when not logged in) |
| `~/.codechecker.instances.json` | `CodeChecker server` (list of running servers) |
| `~/.codechecker/` | Default server workspace and default `store` input folder |
