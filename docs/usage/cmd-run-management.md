# Rename and delete runs (`cmd update`, `cmd del`)

## Rename a run ✅

```sh
CodeChecker cmd update detached -n renamed --url localhost:8555/Default
```

```
[INFO] - Done.
```

The first argument is the current name; `-n` is the new name.

## Delete runs ✅

⚠️ Deleting a run permanently removes its reports, files and history. Single
history entries cannot be deleted; only whole runs.

```sh
CodeChecker cmd del -n renamed --url localhost:8555/Default
CodeChecker cmd del -n run_a run_b --url localhost:8555/Default       # several full names (no wildcards)
CodeChecker cmd del --all-after-run multi --url localhost:8555/Default
CodeChecker cmd del --all-before-run multi --url localhost:8555/Default
CodeChecker cmd del --all-after-time 2026:10:23:12:00:00 --url localhost:8555/Default
CodeChecker cmd del --all-before-time 2026:01:01 --url localhost:8555/Default
```

```
[INFO] - Done.
```

In the test, `--all-after-run multi` deleted `demo2`, the run stored after
`multi`. There is no confirmation prompt.

Exactly one of `-n`, `--all-after-run`, `--all-before-run`,
`--all-after-time`, `--all-before-time` is required.

Exit code: `0` (`1` on errors).
