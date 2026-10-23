# Reference: configuration file (`--config`)

Usage: [../usage/config-file.md](../usage/config-file.md)

Python source: `codechecker_common/cmd_config.py`, `codechecker_common/cli.py`.

The full algorithm is in
[common-infrastructure.md §5](common-infrastructure.md#5-config-file-splicing---config).
Summary of the rules a port must follow:

1. Supported by `analyze`, `check`, `parse`, `server`, `store`.
2. File format by extension: `.yaml`/`.yml` → YAML (all scalars kept as
   strings); anything else → JSON. An unreadable JSON file counts as empty.
3. Section selection:

   | Subcommand | Sections used, in order |
   |---|---|
   | `analyze` | `analyze`, or `analyzer` if `analyze` is empty (warning if both exist) |
   | `check` | (`analyze` or `analyzer`) + `parse` |
   | `parse`, `server`, `store` | the section with the same name |

4. Each entry is one command-line token. Environment variables in entries
   are expanded (`$X`, `${X}`; unknown variables stay as they are).
5. The tokens `--config <file>` are replaced in place by the entries and the
   whole command line is parsed again. The last occurrence of an option wins,
   so:
   - file entries override options written **before** `--config`;
   - options written **after** `--config` override the file.
6. The expanded command line is logged:
   `Full extended command: <program> <args...>`.
7. If the file is missing, no splicing happens. `analyze`, `parse`, `store`
   and `server` then fail with
   `Configuration file '<file>' does not exist.` and exit 1.

Example file and its effect:

```yaml
analyze:
  - --enable=core.DivideZero
  - --disable-all
  - --analyzers=clangsa
parse:
  - --print-steps
```

`CodeChecker check -l cc.json --config f.yaml` is parsed as
`CodeChecker check -l cc.json --enable=core.DivideZero --disable-all --analyzers=clangsa --print-steps`.

Note the order inside the file matters for checker options too: here
`--disable-all` comes after `--enable=core.DivideZero`, but `--disable-all`
is applied as "disable `default` first" (see
[checker-selection.md](checker-selection.md)), so `core.DivideZero` stays
enabled.
