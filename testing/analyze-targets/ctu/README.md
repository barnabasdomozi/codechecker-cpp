# `ctu`

Bugs that are only found with cross translation unit (CTU) analysis, where
the bug depends on a function defined in another source file.

Used for: `analyze --ctu`, the separate `--ctu-collect` and `--ctu-analyze`
phases, `--ctu-ast-mode`, and `check --ctu`
([ctu-analysis.md](../../../docs/usage/ctu-analysis.md)).

## Build

```sh
make
```

## Expected results

### Without CTU

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `src/math.cpp:9:16` | clangsa | `core.DivideZero` | HIGH | `81c09a828d41670f973195d420cea744` | unreviewed |

### With CTU (`analyze --ctu`)

| Location | Analyzer | Checker | Severity | Report hash | Review status |
|---|---|---|---|---|---|
| `src/main.cpp:12:10` | clangsa | `core.NullDereference` | HIGH | `6bd390295f23c0b54ccff75f006e7939` | unreviewed |
| `src/main.cpp:21:10` | clangsa | `core.NullDereference` | HIGH | `18fc94aa5a718201d2bb2cc840ac99bd` | unreviewed |
| `src/math.cpp:4:37` | clangsa | `core.DivideZero` | HIGH | `865e7d753963ded7f9d6d8df74615ead` | unreviewed |
| `src/math.cpp:4:37` | clangsa | `core.DivideZero` | HIGH | `865e7d753963ded7f9d6d8df74615ead` | unreviewed |
| `src/math.cpp:9:16` | clangsa | `core.DivideZero` | HIGH | `81c09a828d41670f973195d420cea744` | unreviewed |
| `src/pipeline_c.cpp:3:34` | clangsa | `core.NullDereference` | HIGH | `e4723311622348a6ece385bd066dcb83` | unreviewed |

- `analyze` exits 0 in both modes. `parse` exits 2.
- The same 6 reports come from `--ctu-collect` followed by `--ctu-analyze`
  on the same output folder, and from `--ctu-ast-mode load-from-pch` and
  `--ctu-ast-mode parse-on-demand`.
- `math.cpp:4:37` is listed twice: once from the analysis of `math.cpp`
  (`scale()` calls `default_divisor()` in `settings.cpp`) and once from
  `main.cpp` (`main()` calls `scale()`).

What each CTU-only report depends on:

| Report | Imported from |
|---|---|
| `src/main.cpp:12` | `find_setting()` in `settings.cpp` sets the pointer to `nullptr` |
| `src/main.cpp:21` | `Counter::reset()` in `settings.cpp` clears `slot` |
| `src/math.cpp:4` | `default_divisor()` in `settings.cpp` returns 0 |
| `src/pipeline_c.cpp:3` | a null pointer passed from `pipeline_a.cpp` through `pipeline_b.cpp` |

`parse --print-steps` shows the path of the `pipeline_c.cpp` report going
through all three files:

```
  Steps:
    1, pipeline_a.cpp:6:16: Assuming 'value' is > 0
    2, pipeline_a.cpp:6:3: 'input' initialized to a null pointer value
    3, pipeline_a.cpp:7:18: Passing null pointer value via 1st parameter 'value'
    4, pipeline_a.cpp:7:10: Calling 'stage_b'
    5, pipeline_b.cpp:3:1: Entered call from 'stage_a'
    6, pipeline_b.cpp:3:42: Passing null pointer value via 1st parameter 'value'
    7, pipeline_b.cpp:3:34: Calling 'stage_c'
    8, pipeline_c.cpp:3:1: Entered call from 'stage_b'
    9, pipeline_c.cpp:3:34: Dereference of null pointer (loaded from variable 'value')
```

## Note for adding bugs

Clang SA hides null-pointer bugs where the null value is **returned** from
another function (the `suppress-null-return-paths` analyzer option, on by
default). A function like `int *f() { return nullptr; }` in another file
gives no report, even with CTU. So the bugs here pass the null pointer in
through a parameter or set it through a reference.
