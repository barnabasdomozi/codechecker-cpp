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

| Location | Function | Analyzer | Checker | Report hash | Review status |
|---|---|---|---|---|---|
| `comments.cpp:6:12` | `no_comment` | clangsa | `core.DivideZero` | `88567cf6f5f144b85c8ad3c4d42114cd` | unreviewed |
| `comments.cpp:6:12` | `no_comment` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:12:12` | `suppress_marker` | clangsa | `core.DivideZero` | `2ec53ed333ebef9a89cc8b66324963af` | false_positive |
| `comments.cpp:12:12` | `suppress_marker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:18:12` | `false_positive_marker` | clangsa | `core.DivideZero` | `57f6156a634c130ec573502042035735` | false_positive |
| `comments.cpp:18:12` | `false_positive_marker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:24:12` | `intentional_marker` | clangsa | `core.DivideZero` | `e8db3cff2a04004a3860a524af359093` | intentional |
| `comments.cpp:24:12` | `intentional_marker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:30:12` | `confirmed_marker` | clangsa | `core.DivideZero` | `d6149106441223ecfb17bed66d8603ba` | confirmed |
| `comments.cpp:30:12` | `confirmed_marker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:36:12` | `all_checkers` | clangsa | `core.DivideZero` | `af8b5488cb3222fb983f0bf3c431a3ee` | false_positive |
| `comments.cpp:36:12` | `all_checkers` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | false_positive |
| `comments.cpp:42:12` | `substring_match` | clangsa | `core.DivideZero` | `7c1f51161e057ce55613ed9171db8f8c` | intentional |
| `comments.cpp:42:12` | `substring_match` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:48:12` | `other_checker` | clangsa | `core.DivideZero` | `8ca244f8328d35f3418dbcbbe20b31c5` | unreviewed |
| `comments.cpp:48:12` | `other_checker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:54:12` | `checker_list` | clangsa | `core.DivideZero` | `488407d16ca869d897aa0d5ba0fa1639` | confirmed |
| `comments.cpp:54:12` | `checker_list` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:61:12` | `multi_line` | clangsa | `core.DivideZero` | `11e137793c4f5bf4b75348b911c063a1` | intentional |
| `comments.cpp:61:12` | `multi_line` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:68:12` | `c_style` | clangsa | `core.DivideZero` | `8cfa9dbf8246a379453bcbfe56db2306` | false_positive |
| `comments.cpp:68:12` | `c_style` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:74:12` | `missing_reason` | clangsa | `core.DivideZero` | `1c999541c53ba6a9bf182b06fc65c138` | confirmed |
| `comments.cpp:74:12` | `missing_reason` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:80:12` | `misspelled_marker` | clangsa | `core.DivideZero` | `eea0be19368155e064d9fcd61fa6a3c1` | unreviewed |
| `comments.cpp:80:12` | `misspelled_marker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:86:12` | `not_a_marker` | clangsa | `core.DivideZero` | `3ece47aaa18304eecdacbc06d45db074` | unreviewed |
| `comments.cpp:86:12` | `not_a_marker` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `comments.cpp:92:12` | `not_adjacent` | clangsa | `core.DivideZero` | `72770300dcb8a05f8f8c26fa782edfd6` | unreviewed |
| `comments.cpp:92:12` | `not_adjacent` | cppcheck | `cppcheck-zerodiv` | `f83e4dba624b51c9f11d175de9559ded` | unreviewed |
| `rules_target.cpp:5:12` | `by_hash` | clangsa | `core.DivideZero` | `a3b0cc7289e197ae3e7026c2e59ceff7` | unreviewed |
| `rules_target.cpp:5:12` | `by_hash` | cppcheck | `cppcheck-zerodiv` | `1501a3d5e7112e4be050b95917254dd3` | unreviewed |
| `rules_target.cpp:10:12` | `by_path_and_checker` | clangsa | `core.DivideZero` | `b0d5f20e9642c189b2fda27235dee5b0` | unreviewed |
| `rules_target.cpp:10:12` | `by_path_and_checker` | cppcheck | `cppcheck-zerodiv` | `1501a3d5e7112e4be050b95917254dd3` | unreviewed |
| `rules_target.cpp:15:10` | `by_checker` | clangsa | `core.NullDereference` | `32f6f1e41328512a80f6c978b25af21e` | unreviewed |
| `rules_target.cpp:15:11` | `by_checker` | cppcheck | `cppcheck-nullPointer` | `20fc68f3cabcd05b5cc7a91cd234f3b2` | unreviewed |
| `rules_target.cpp:21:12` | `comment_wins` | clangsa | `core.DivideZero` | `061515c43b4389810551732fa6756cdc` | intentional |
| `rules_target.cpp:21:12` | `comment_wins` | cppcheck | `cppcheck-zerodiv` | `1501a3d5e7112e4be050b95917254dd3` | unreviewed |

All reports have severity HIGH. Cppcheck reports the same divisions by zero
(and the null dereference) at the same places. Only `all_checkers` (`[all]`)
hides a Cppcheck report: the other comments name Clang SA checkers.
Cppcheck's hash does not depend on the function, so every
`return a / z;` division in a file has the same Cppcheck hash.

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

| Location | Checker | Review status | Matched by |
|---|---|---|---|
| `rules_target.cpp:5:12` | `core.DivideZero` | confirmed | rule 1: `report_hash` prefix `a3b0cc72` |
| `rules_target.cpp:5:12` | `cppcheck-zerodiv` | unreviewed | no rule (the rules name Clang SA checkers and hashes) |
| `rules_target.cpp:10:12` | `core.DivideZero` | false_positive | rule 2: `filepath` + `checker_name` |
| `rules_target.cpp:10:12` | `cppcheck-zerodiv` | unreviewed | no rule (the rules name Clang SA checkers and hashes) |
| `rules_target.cpp:15:10` | `core.NullDereference` | intentional | rule 3: `checker_name` |
| `rules_target.cpp:15:11` | `cppcheck-nullPointer` | unreviewed | no rule (the rules name Clang SA checkers and hashes) |
| `rules_target.cpp:21:12` | `core.DivideZero` | intentional | its source comment; rule 2 also matches, but comments win |
| `rules_target.cpp:21:12` | `cppcheck-zerodiv` | unreviewed | no rule (the rules name Clang SA checkers and hashes) |

Line 5 matches rules 1 and 2. The first matching rule wins.

### With the suppress file

```sh
CodeChecker parse reports --suppress suppress.txt
```

- Hidden: the Clang SA reports on `comments.cpp:6` (full line with status
  `false_positive`) and `comments.cpp:80` (line without a status field,
  which counts as `false_positive`). The entries have Clang SA hashes, so
  the Cppcheck reports on these lines stay.
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
