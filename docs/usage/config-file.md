# Put options in a configuration file (`--config`)

Instead of long command lines, options can be stored in a YAML or JSON file
with one section per subcommand. These commands accept `--config FILE`:
`analyze`, `check`, `parse`, `server`, `store`.

Options from the file are **added after** the command-line options, so a value
in the file wins over the same option on the command line ✅. For example,
`analyze --analyzers clang-tidy --config codechecker.yaml` with the file below
runs only `clangsa`. Environment
variables in the file (`$HOME`, `${VAR}`) are expanded.

## YAML file ✅

`codechecker.yaml`:

```yaml
analyze:
  - --enable=core.DivideZero
  - --disable-all
  - --analyzers=clangsa
  - --report-hash=context-free-v2
parse:
  - --print-steps
```

```sh
CodeChecker analyze compile_commands.json -o reports --config codechecker.yaml
CodeChecker parse reports --config codechecker.yaml
```

```
[HIGH] ~/demo/main.c:11:12: Division by zero [core.DivideZero]
  return a / z;
           ^
  Report hash: d11a6fc279b8946a4bdc0365ff032ae0
  Steps:
    1, main.c:22:18: Calling 'local_div'
    2, main.c:9:1: Entered call from 'main'
    3, main.c:10:3: 'z' initialized to 0
    4, main.c:11:12: Division by zero
```

Write each option as `--option=value`, or as two list items, `- --option`
followed by `- value` ✅.

## JSON file ✅

`codechecker.json`:

```json
{ "analyze": ["--analyzers=clangsa", "--disable-all", "--enable=core.DivideZero"] }
```

```sh
CodeChecker analyze compile_commands.json -o reports --config codechecker.json
```

Both `.yaml` and `.json` files work.

## Store settings ✅

```yaml
store:
  - --name=cfg_run
  - --tag=from-config
  - --url=localhost:8555/Default
```

```sh
CodeChecker store reports --config client.yaml
```

The run `cfg_run` with tag `from-config` appears on the server.

## Server settings ✅

```yaml
server:
  - --skip-db-cleanup
```

```sh
CodeChecker server -w ./ws --config server.yaml
```

```
[INFO] - Extending command line options with server options from 'server.yaml' file: --skip-db-cleanup
[INFO] - Full extended command: .../cli.py server -w ./ws --skip-db-cleanup ...
```

## `check` uses the `analyze` and `parse` sections

`CodeChecker check --config FILE` reads both the `analyze` and the `parse`
sections ✅. With the YAML file above, `check` runs only `core.DivideZero` and
prints the bug steps.
