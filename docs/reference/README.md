# CodeChecker reference implementation

These pages describe **how** the Python CodeChecker 6.30.0
([Ericsson/codechecker](https://github.com/Ericsson/codechecker/tree/451b0c5c1786ed21b0685c298f759164a8c5d379), commit `451b0c5c`) implements each use case of [../usage/](../usage/README.md):
algorithms, file formats, wire formats, database schema, messages and exit
codes, precisely enough to re-implement them in another language. Each
page names the Python source files it was derived from (paths relative to
that repository); behavior was
checked against a real 6.30.0 build where possible, and pages say
"verified" where a detail was tested.

Where the reference implementation has a bug, the page describes the
actual behavior and marks it with ⚠️, usually with the intended behavior a
port should implement.

## Shared pages

| Page | Content |
|---|---|
| [common-infrastructure.md](common-infrastructure.md) | launcher, subcommands, argument parsing, `--config`, logging, table output |
| [report-format.md](report-format.md) | report model, plist format, result folder files, changed-file detection |
| [report-hash.md](report-hash.md) | report hash algorithms |
| [checker-labels.md](checker-labels.md) | checker label files (severity, profile, guideline) |
| [analyzer-drivers.md](analyzer-drivers.md) | how each analyzer is invoked and its output read |
| [server-api.md](server-api.md) | Thrift API, HTTP layer, URLs, sessions and credentials, client error handling, `cmd login` |

## Usage page → reference page

| Usage | Reference |
|---|---|
| [version](../usage/version.md) | [version.md](version.md) |
| [analyzers](../usage/analyzers.md) | [analyzers.md](analyzers.md) |
| [checkers](../usage/checkers.md) | [checkers.md](checkers.md), [checker-labels.md](checker-labels.md) |
| [log](../usage/log.md) | [log.md](log.md) |
| [analyze](../usage/analyze.md) | [analyze.md](analyze.md), [analyzer-drivers.md](analyzer-drivers.md) |
| [checker-selection](../usage/checker-selection.md) | [checker-selection.md](checker-selection.md) |
| [analyzer-configuration](../usage/analyzer-configuration.md) | [analyzer-configuration.md](analyzer-configuration.md) |
| [compiler-info](../usage/compiler-info.md) | [compiler-info.md](compiler-info.md) |
| [config-file](../usage/config-file.md) | [config-file.md](config-file.md) |
| [skip-and-file-filters](../usage/skip-and-file-filters.md) | [skip-and-file-filters.md](skip-and-file-filters.md) |
| [ctu-analysis](../usage/ctu-analysis.md) | [ctu-analysis.md](ctu-analysis.md) |
| [statistical-analysis](../usage/statistical-analysis.md) | [statistical-analysis.md](statistical-analysis.md) |
| [makefile-mode](../usage/makefile-mode.md) | [makefile-mode.md](makefile-mode.md) |
| [analysis-debugging](../usage/analysis-debugging.md) | [analysis-debugging.md](analysis-debugging.md) |
| [check](../usage/check.md) | [check.md](check.md) |
| [parse](../usage/parse.md) | [parse.md](parse.md) |
| [parse-export](../usage/parse-export.md) | [parse-export.md](parse-export.md) |
| [review-status-and-suppression](../usage/review-status-and-suppression.md) | [review-status-and-suppression.md](review-status-and-suppression.md) |
| [fixit](../usage/fixit.md) | [fixit.md](fixit.md) |
| [local-diff](../usage/local-diff.md) | [local-diff.md](local-diff.md) |
| [server](../usage/server.md) | [server.md](server.md) |
| [server-database](../usage/server-database.md) | [server-database.md](server-database.md) |
| [server-authentication](../usage/server-authentication.md) | [server-authentication.md](server-authentication.md) |
| [store](../usage/store.md) | [store.md](store.md) |
| [web-ui](../usage/web-ui.md) | [web-ui.md](web-ui.md) |
| [cmd-runs-history](../usage/cmd-runs-history.md) | [cmd-runs-history.md](cmd-runs-history.md) |
| [cmd-results](../usage/cmd-results.md) | [cmd-results.md](cmd-results.md) (also the shared report filter) |
| [cmd-sum](../usage/cmd-sum.md) | [cmd-sum.md](cmd-sum.md) |
| [cmd-diff](../usage/cmd-diff.md) | [cmd-diff.md](cmd-diff.md) |
| [cmd-run-management](../usage/cmd-run-management.md) | [cmd-run-management.md](cmd-run-management.md) |
| [cmd-suppress](../usage/cmd-suppress.md) | [cmd-suppress.md](cmd-suppress.md) |
| [cmd-export-import](../usage/cmd-export-import.md) | [cmd-export-import.md](cmd-export-import.md) |
| [cmd-products](../usage/cmd-products.md) | [cmd-products.md](cmd-products.md) |
| [cmd-components](../usage/cmd-components.md) | [cmd-components.md](cmd-components.md) |
| [cmd-filter-preset](../usage/cmd-filter-preset.md) | [cmd-filter-preset.md](cmd-filter-preset.md) |
| [cmd-serverside-tasks](../usage/cmd-serverside-tasks.md) | [cmd-serverside-tasks.md](cmd-serverside-tasks.md) |
| [report-converter](../usage/report-converter.md) | [report-converter.md](report-converter.md) |
| [tu-collector](../usage/tu-collector.md) | [tu-collector.md](tu-collector.md) |
| [merge-clang-extdef-mappings](../usage/merge-clang-extdef-mappings.md) | [merge-clang-extdef-mappings.md](merge-clang-extdef-mappings.md) |
| [gerrit-integration](../usage/gerrit-integration.md) | [gerrit-integration.md](gerrit-integration.md) |
| [memory-safety-reporter](../usage/memory-safety-reporter.md) | [memory-safety-reporter.md](memory-safety-reporter.md) |
| [bazel-compile-commands](../usage/bazel-compile-commands.md) | [bazel-compile-commands.md](bazel-compile-commands.md) |
| [environment-variables](../usage/environment-variables.md) | [environment-variables.md](environment-variables.md) |
| [exit-codes](../usage/exit-codes.md) | [exit-codes.md](exit-codes.md) |
| [demo-project](../usage/demo-project.md) | [demo-project.md](demo-project.md) (expected results as test fixtures) |
| [known-issues](../usage/known-issues.md) | issues are described on the pages above |
