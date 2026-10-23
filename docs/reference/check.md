# Reference: `CodeChecker check`

Usage: [../usage/check.md](../usage/check.md)

Python source: `analyzer/codechecker_analyzer/cli/check.py`.

`check` = `log` (optional) + `analyze` + `parse`, run in one process by
calling the three command implementations with translated arguments.

## 1. Options

Same as `analyze` ([analyze.md](analyze.md) §1) with these differences:
- input: exactly one of `-b/--build COMMAND` or `-l/--logfile FILE`;
- `-o/--output` is optional (dest `output_dir`);
- `--use-absolute-ldpreload-path` (for the log step);
- `-q/--quiet` applies to both log (build output) and analyze;
- parse options: `--print-steps`, `--suppress FILE`,
  `--trim-path-prefix [P...]`, `--review-status [S...]`;
- no `-n/--name`, no `--makefile`, no export options.

`--config` sections: `analyze` (or `analyzer`) followed by `parse`
([config-file.md](config-file.md)).

## 2. Flow

1. Set up logging. `--ctu-ast-mode` without a CTU phase →
   `Analyzer option 'ctu-ast-mode' requires CTU mode enabled`, exit 1.
2. Output dir: `-o` value, or a new temporary directory
   (`mkdtemp`). Made absolute, created if missing.
3. **Log step** (only with `-b`): log file = a new temporary file name. If
   `CC_LOGGER_DEBUG_FILE` is not set in the environment, set it to
   `<output dir>/codechecker.logger.debug` (so parallel checks do not share
   one debug file; it is only used with debug verbosity). Run `log` with
   `command`, `logfile`, and `quiet`, `verbose`,
   `use_absolute_ldpreload_path` when given ([log.md](log.md)). A failing
   build exits the whole command with the build's exit code (the log step
   calls exit).
   With `-l`: log file = the given file.
4. **Analyze step**: run `analyze` with `input = log file`,
   `output_path = output dir`, `output_format = plist`, `jobs`,
   `keep_gcc_include_fixed`, `keep_gcc_intrin`, plus every other analyze
   option that was given (`quiet`, `skipfile`, `drop_skipped_reports`,
   `files`, `analyzers`, deprecated args files, `analyzer_config`,
   `checker_config`, `capture_analysis_output`, `generate_reproducer`,
   `config_file`, all CTU and statistics options, `enable_all`,
   `disable_all`, `no_missing_checker_error`, the ordered `-e/-d` list,
   `timeout`, `review_status_config`, `compile_uniqueing`, `report_hash`,
   `add_gcc_include_dirs_with_isystem`, `compiler_info_file`,
   `dump_compiler_info_file`, `enable_z3_refutation`) and `clean` if `-c`.
   Its return value (0 or 3) is remembered. Exits inside analyze (errors →
   1, "nothing to analyze" → 0) end the whole command immediately (after the
   cleanup in step 6).
5. **Parse step**: run `parse` with `input = [output dir]` and, when given,
   `print_steps`, `trim_path_prefix`, `review_status`, `verbose`,
   `skipfile`, `suppress`. (Note: `--file` is not passed to parse.) Parse
   prints the reports, the statistics tables and the status summary
   ([parse.md](parse.md)).
6. Cleanup (always): delete the temporary output dir (only if `-o` was not
   given) and the temporary log file (only with `-b`).
7. Errors: an import error → `Check failed: couldn't import a library.`;
   any other exception → `Running check failed.` with a stack trace; exit
   code then is the remembered analyze status, or 1 if analyze did not
   finish.

## 3. Exit code

| Situation | Exit |
|---|---|
| parse printed at least one report | **2** (parse's own exit, even if some analysis failed) |
| no reports, all analyses succeeded | 0 |
| no reports, some analysis failed | 3 |
| build failed (`-b`) | the build's exit code |
| argument/config/analyzer errors | 1 |
