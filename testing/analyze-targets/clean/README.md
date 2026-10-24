# `clean`

A small, working C++17 program (word statistics from standard input) with
no reports under the default checkers.

Used for the "nothing found" paths: `parse` and `check` exit 0, empty
exports, `cmd diff` with an empty result, storing a run with no reports.

## Build

```sh
make
echo "x y x" | ./wordstat   # prints "3 words, 2 distinct", then "x 2", "y 1"
```

## Expected results

- No reports, also with `--ctu`.
- `analyze` exits 0. `parse` exits 0 and prints `Found no defects in <file>`
  for `main.cpp`, `tokenizer.cpp` and `word_count.cpp`, then a summary with
  6 processed result files and 0 reports.
- `CodeChecker check -b make` exits 0.

`-e profile:sensitive` also gives no reports. `-e profile:extreme` gives
23 style reports, so the project is not clean under every profile.
