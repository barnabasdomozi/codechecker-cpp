# Reference: checker selection (`-e`, `-d`, `--enable-all`, `--disable-all`)

Usage: [../usage/checker-selection.md](../usage/checker-selection.md)

Python source: `analyzer/codechecker_analyzer/analyzers/config_handler.py`,
`arg.py` (`OrderedCheckersAction`), `cli/analyze.py`, `analyzer.py`,
`checkers.py`, label data in [checker-labels.md](checker-labels.md).

## 1. Input

Every `-e X` / `-d X` is appended, in command-line order, to one list of
`(X, enabled)` pairs. `--disable-all` inserts `("default", false)` at the
**front** of this list. `--enable-all` is a separate flag.

## 2. State table (per analyzer)

Each analyzer builds an **ordered** table `checker → (state, description)`:

1. Add every checker of the analyzer (from the binary, see
   [analyzer-drivers.md](analyzer-drivers.md#checker-lists)) as
   **disabled**, in the analyzer's listing order.
2. **Default profile**: for every checker name that has label
   `profile:default` in **any** analyzer's label file, enable the table entry
   with exactly that name (strict match). If no checker has
   `profile:default`, warn `No default profile found!`.
3. **`--enable-all`**: for each checker (in listing order) that does **not**
   start with any of
   `alpha. debug. osx. abseil- android- darwin- objc- cppcoreguidelines-
   fuchsia. fuchsia- hicpp- llvm- llvmlibc- google- zircon-`, enable it
   (non-strict, see §3). On macOS, `osx.` checkers are also enabled.
4. **Command-line items**, in order (§4).

Enabling/disabling moves the affected entries to the **end** of the table
(delete + re-insert). This matters because the table order is the order in
which analyzers receive checkers (e.g. the order of clang-tidy's
`-checks` list and `-W` flags), so later decisions come later.

## 3. Name matching

`set(name, enabled, strict)`:
- strict: only the entry whose name equals `name`;
- non-strict: every entry matching the regex `^<escaped name>\b.*$`. The word
  boundary means `core` matches `core.DivideZero` and `core-x`, but
  `core.uninit` does **not** match `core.uninitialized.Assign`, and
  `bugprone-string` matches `bugprone-string-constructor` (boundary at `-`).

## 4. Resolving a command-line item `X`

Namespaces:
- `checker` → the table's checker names;
- `prefix` → all proper prefixes of checker names: each name is split at `.`
  if it contains a `.`, else at `-`, and every leading part-sequence except
  the full name is a prefix (`security`, `security.insecureAPI` from
  `security.insecureAPI.gets`; `misc`, `misc-dangling` from
  `misc-dangling-handle`);
- every **label key** that occurs in any label file (`profile`, `severity`,
  `guideline`, `sei-cert-c`, `doc_url`, ...) → that label's occurring values.

Rules:
1. `X` contains `:` → split at the first `:` into `ns:value`.
   - `ns` not a known namespace → error
     `The <ns> namespace is not known. Please selectone of these existing namespace options: <list>.`
     (sic, no space), exit 1.
   - For label namespaces, `value` must be an occurring value, else error
     `The <value> identifier does not exist in the <ns> namespace. Please select one of these existing options: <list>.`,
     exit 1. (`checker:` and `prefix:` values are not checked here, because
     each analyzer only knows its own checkers.)
   - Apply (§5).
2. No `:` → find every namespace in which `X` occurs.
   - exactly one → apply in that namespace;
   - more than one → error
     `The X is ambiguous. Please select one of these options to clarify the checker list: ns1:X, ns2:X.`,
     exit 1;
   - none → ignore here (the unknown-name check in
     [analyze.md](analyze.md) §4 step 7 reports it).

   So the state table accepts a bare value of **any** label: `-e HIGH`
   enables every checker with `severity:HIGH`. However, the later
   unknown-name check ([analyze.md](analyze.md) §4 step 7) only knows checker
   names and **profile** names. Verified:
   - `-e sensitive` (a profile) works;
   - `-e HIGH` and `-e sei-cert-c` fail with
     `No checker(s) with these names was found: HIGH.` (exit 1); with
     `--no-missing-checker-error` the warning is printed and `-e HIGH` does
     enable the 69 HIGH clangsa checkers;
   - use `-e severity:HIGH` / `-e guideline:sei-cert-c` instead (values with
     `:` skip the unknown-name check).

   Since each analyzer resolves the item against its own checker names, an
   item can be a checker in one analyzer and a label value in another without
   conflict.

## 5. Applying

| Namespace | Effect |
|---|---|
| `checker` | strict set of that name |
| `prefix` | non-strict set (§3) |
| label `k` | for every checker in `checkers_by_labels(["k:value"])` (all analyzers), strict set |

## 6. Statistics checkers (clangsa only)

After the command-line items:
- with `--stats`: find the first checker containing
  `statisticsbased.SpecialReturnValue` and the first containing
  `statisticsbased.UncheckedReturnValue`; enable each unless the user
  disabled it (some `-d D` with the checker name starting with `D`);
- always disable (non-strict) `statisticscollector.SpecialReturnValue` and
  `statisticscollector.ReturnValueCheck`.

## 7. Compiler warnings

Compiler warnings are ordinary checkers named `clang-diagnostic-<warning>`
(clang-tidy) and `gcc-<warning>` (gcc). There is no special handling of
`W<name>` items: `-e Wunused-variable` matches no checker and fails the
unknown-name check (the help text's `-e Wliteral-conversion` example does not
work).

How the states reach the tools is described in
[analyzer-drivers.md](analyzer-drivers.md) §5.

## 8. Unknown names

See [analyze.md](analyze.md) §4 step 7. Note the asymmetry: a value that is
unknown to **all** namespaces of an analyzer is silently ignored by that
analyzer, and only reported later if it also matches no checker of any
running analyzer and is not a profile name.

## 9. Example

`--disable-all -e core.DivideZero` becomes
`[("default", off), ("core.DivideZero", on)]`:
- `default` occurs only as a `profile` value → disable all default-profile
  checkers;
- `core.DivideZero` is a clangsa checker → enable it (strict). In clang-tidy
  it is in no namespace → ignored there.
- clang-tidy then has no enabled checker and is skipped
  (`No checkers enabled for clang-tidy`).
