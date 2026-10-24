# `review-status`

One bug per kind of source-code review status comment, plus a rules file
(`review_status.yaml`) and a suppress file (`suppress.txt`).

Used for:
[review-status-and-suppression.md](../../../docs/usage/review-status-and-suppression.md),
`parse --review-status`, `parse --suppress`, `--export-source-suppress`,
and review statuses after `store` and in `cmd results` / `cmd diff`.

## Build

```sh
make             # comments.cpp, rules_target.cpp
make ambiguous   # extra/ambiguous.cpp only (see below)
```

## Expected results (`make`)

| Location | Function | Checker | Report hash | Review status |
|---|---|---|---|---|
| `comments.cpp:6:12` | `no_comment` | `core.DivideZero` | `88567cf6f5f144b85c8ad3c4d42114cd` | unreviewed |
| `comments.cpp:12:12` | `suppress_marker` | `core.DivideZero` | `2ec53ed333ebef9a89cc8b66324963af` | false_positive |
| `comments.cpp:18:12` | `false_positive_marker` | `core.DivideZero` | `57f6156a634c130ec573502042035735` | false_positive |
| `comments.cpp:24:12` | `intentional_marker` | `core.DivideZero` | `e8db3cff2a04004a3860a524af359093` | intentional |
| `comments.cpp:30:12` | `confirmed_marker` | `core.DivideZero` | `d6149106441223ecfb17bed66d8603ba` | confirmed |
| `comments.cpp:36:12` | `all_checkers` | `core.DivideZero` | `af8b5488cb3222fb983f0bf3c431a3ee` | false_positive |
| `comments.cpp:42:12` | `substring_match` | `core.DivideZero` | `7c1f51161e057ce55613ed9171db8f8c` | intentional |
| `comments.cpp:48:12` | `other_checker` | `core.DivideZero` | `8ca244f8328d35f3418dbcbbe20b31c5` | unreviewed |
| `comments.cpp:54:12` | `checker_list` | `core.DivideZero` | `488407d16ca869d897aa0d5ba0fa1639` | confirmed |
| `comments.cpp:61:12` | `multi_line` | `core.DivideZero` | `11e137793c4f5bf4b75348b911c063a1` | intentional |
| `comments.cpp:68:12` | `c_style` | `core.DivideZero` | `8cfa9dbf8246a379453bcbfe56db2306` | false_positive |
| `comments.cpp:74:12` | `missing_reason` | `core.DivideZero` | `1c999541c53ba6a9bf182b06fc65c138` | confirmed |
| `comments.cpp:80:12` | `misspelled_marker` | `core.DivideZero` | `eea0be19368155e064d9fcd61fa6a3c1` | unreviewed |
| `comments.cpp:86:12` | `not_a_marker` | `core.DivideZero` | `3ece47aaa18304eecdacbc06d45db074` | unreviewed |
| `comments.cpp:92:12` | `not_adjacent` | `core.DivideZero` | `72770300dcb8a05f8f8c26fa782edfd6` | unreviewed |
| `rules_target.cpp:5:12` | `by_hash` | `core.DivideZero` | `a3b0cc7289e197ae3e7026c2e59ceff7` | unreviewed |
| `rules_target.cpp:10:12` | `by_path_and_checker` | `core.DivideZero` | `b0d5f20e9642c189b2fda27235dee5b0` | unreviewed |
| `rules_target.cpp:15:10` | `by_checker` | `core.NullDereference` | `32f6f1e41328512a80f6c978b25af21e` | unreviewed |
| `rules_target.cpp:21:12` | `comment_wins` | `core.DivideZero` | `061515c43b4389810551732fa6756cdc` | intentional |

All reports are from clangsa with severity HIGH.

- `analyze` exits 0. `parse` exits 2. Plain `parse` prints only the
  `unreviewed` and `confirmed` reports.
- `parse` prints a warning for `misspelled_marker`:
  `.../comments.cpp contains misspelled review status comment @79: // codechecker_suppressss [all] ...`.
  `not_a_marker` (`codechecker_supress`) gives no warning, because it does
  not contain a full marker name.

### With the rules file

```sh
CodeChecker analyze compile_commands.json -o reports \
  --review-status-config review_status.yaml
```

`analyze` copies the file to `reports/review_status.yaml`. The
`comments.cpp` results do not change. `rules_target.cpp` becomes:

| Location | Review status | Matched by |
|---|---|---|
| `rules_target.cpp:5:12` | confirmed | rule 1: `report_hash` prefix `a3b0cc72` |
| `rules_target.cpp:10:12` | false_positive | rule 2: `filepath` + `checker_name` |
| `rules_target.cpp:15:10` | intentional | rule 3: `checker_name` |
| `rules_target.cpp:21:12` | intentional | its source comment; rule 2 also matches, but comments win |

Line 5 matches rules 1 and 2. The first matching rule wins.

### With the suppress file

```sh
CodeChecker parse reports --suppress suppress.txt
```

- Hidden: `comments.cpp:6` (full line with status `false_positive`) and
  `comments.cpp:80` (line without a status field, which counts as
  `false_positive`).
- Not hidden: `comments.cpp:86` (old format without a file name never
  matches) and `comments.cpp:92` (the file name in the entry is different).
- Warning: `Malformed suppress line: this line is malformed`.
- `parse` still exits 2.

## Ambiguous comments (`make ambiguous`)

`extra/ambiguous.cpp` has two comments for the same report. `analyze` exits
0. `parse` prints
`Multiple source code comments can be found for 'core.DivideZero' checker in '.../extra/ambiguous.cpp' at line 7.`
and exits 1, also with `-e json` (no JSON is written). It is kept out of
`make` so that `parse` works on the rest of the project.
