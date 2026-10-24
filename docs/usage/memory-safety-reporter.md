# Package results for memory safety reporting (`memory_safety_reporter`)

`memory_safety_reporter` packs a report folder into an archive with a fixed
layout and a name built from product information. It is meant for delivering
memory-safety analysis evidence. The tool is in the `bin/` folder of the
package (source:
[`tools/memory-safety-reporter`](https://github.com/Ericsson/codechecker/tree/451b0c5c1786ed21b0685c298f759164a8c5d379/tools/memory-safety-reporter)).

```
memory_safety_reporter -r REPORT_DIRECTORY [-o OUTPUT_FILE] [-p PRODUCT_NAME]
                       [-v REVISION] [-b BINARY_NAME] [-d BUILD_ID]
                       [-t TIMESTAMP] [-x {gztar,zip}]
```

## Example ✅

```sh
memory_safety_reporter -r ./reports -o ./msr -p CXC123456 -v R1A01 -b app \
  -d build-42 -t 2026-10-23T23:00:00 -x zip
```

```
Creating package with name:  ./msr_CXC123456_R1A01_app_build-42_2026-10-23T23:00:00
```

The package name is `-o` followed by the given `-p -v -b -d -t` values,
joined with `_`. The product information appears **only in the name**, not
inside the files. The package contains:

```
msr_CXC123456_R1A01_app_build-42_2026-10-23T23:00:00/
├── reports.sarif            # CodeChecker parse REPORT_DIR -e sarif (its status summary is printed)
├── metadata.json            # copy of REPORT_DIR/metadata.json (the analysis metadata)
├── checker_details.json     # CodeChecker checkers --guideline memory-safety -o json --details
├── config/                  # copy of REPORT_DIR/conf (analyzer argument files; empty in the test)
└── CHECKSUMS.sha256         # "<sha256>\t<relative path>" for every other file
msr_CXC123456_R1A01_app_build-42_2026-10-23T23:00:00.zip
```

If the report folder does not exist: `Report directory '...' does not exist!`
(exit code 1).

| Option | Meaning |
|---|---|
| `-r` | Report folder (required) |
| `-o` | Output name prefix |
| `-p` | Product or system ID, e.g. `CXC123456` |
| `-v` | Product revision ("RState"), e.g. `R10S25` |
| `-b` | Binary name |
| `-d` | Build or pipeline ID (CI job ID, commit hash, ...) |
| `-t` | UTC timestamp of the end of the analysis |
| `-x` | Archive type: `gztar` (`.tar.gz`) or `zip` |

Exit code `0`.
