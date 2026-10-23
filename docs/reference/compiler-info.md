# Reference: compilation database parsing and compiler info

Usage: [../usage/compiler-info.md](../usage/compiler-info.md)

Python source: `analyzer/codechecker_analyzer/buildlog/log_parser.py`,
`buildlog/build_action.py`, `gcc_toolchain.py`.

This page describes how `analyze`/`check` turn compilation database entries
into **build actions** (the input of [analyzer-drivers.md](analyzer-drivers.md)),
including implicit compiler information, filtering and uniqueing.

## 1. Response files

Before parsing, each entry whose `command` contains `@`:
- shell-split the command; for each `@F` token: if `directory/F` exists,
  replace the token with the shell-quoted options read from `F` (the file is
  shell-split; its non-option tokens with a source extension
  `.c .cc .cp .cpp .cxx .c++ .o .so .a` are collected as sources); else if
  `directory/@F` exists, keep the token; else warn
  `Response file '<path>' does not exists.` and drop it.
- re-join as the new `command`;
- if `file` itself is `@F` and `F` was expanded, emit one entry per collected
  source instead of the original entry.

## 2. Parsing one entry → build action

Fields start as: `analyzer_options=[]`, `compiler_includes=[]`,
`compiler_standard=""`, `target=""`, `arch=""`, `lang=None`, `output=""`,
`action_type=None`.

1. Command: `arguments` list (then `original_command` = shell-joined
   arguments) or `command` string (shell-split). Neither → error, entry
   dropped and counted as skipped.
2. **Compiler**: first token; if it ends with `ccache` and the second token
   is an executable found in `PATH`, the compiler is the second token. A
   relative compiler path containing `/` is resolved against `directory`.
3. If the compiler's base name contains `++` → `lang = c++`.
4. Choose the flag processors:
   - **Clang mode** if the canonical compiler binary's file name contains
     `clang`. If its directory equals the analyzer clang's directory, note
     "same clang".
   - **GCC mode** otherwise.
5. Walk the arguments after the compiler; for each, the first processor that
   accepts it wins (processors may consume the next argument):

   GCC mode order:
   1. **skip** if it matches the GCC ignore regex (§2.1) — also skip a fixed
      number of following arguments for the "param" options (§2.1);
   2. **replace** `-mips32` → `-target mips -mips32`, `-mips64` →
      `-target mips64 -mips64`, `-mpowerpc` → `-target powerpc`,
      `-mpowerpc64` → `-target powerpc64`;
   3. **include-like options** (§2.2);
   4. **compile options**: keep if it matches
      `-nostdinc|-nostdinc\+\+|-pedantic|-O[1-3]|-Os|-{1,2}std=|-{1,2}stdlib=|-{1,2}target=|-f|-m|-Wno-|--sysroot=|-sdkroot|--gcc-toolchain=|-pthread`
      (prefix match);
   5. **action type**: `-c` → COMPILE; `-print-prog-name...` → INFO (unless
      already COMPILE); `-(E|M[G|T|Q|F|J|P|V|M]*)$` → PREPROCESS (unless
      already COMPILE);
   6. **sources**: any argument not starting with `-` is dropped;
   7. `-arch X` → `arch = X`;
   8. `--target X` / `-target X` → target;
   9. `-x L` / `-xL` → `lang = L`;
   10. `-o X` → `output = X`.
   Anything else is dropped (e.g. `-Wall`, `-g`, `-DNDEBUG`, linker flags).

   Clang mode order:
   1. drop non-option arguments (sources);
   2. skip `-Werror`, `-pedantic-errors`, `-w` (prefix match);
   3. `-Xclang Y`: drop both if `Y` is one of `-module-file-info -S -emit-llvm
      -emit-llvm-bc -emit-llvm-only -emit-llvm-uselists -rewrite-objc`,
      else keep both;
   4. `-o X`; 5. action type; 6. `-arch`; 7. `--target/-target`; 8. `-x`;
   9. include-like options (§2.2);
   10. **keep everything else**.
6. No action type seen → COMPILE.
7. `source` = `file`, made absolute against `directory` and canonicalized if
   relative. `"."` → empty.
8. Language from the source extension: `.c`→c; `.cp .cpp .cxx .txx .cc .C .ii`
   → c++; `.m` → objective-c; `.mm` → objective-c++. Unknown extension →
   action type LINK (dropped later). Known → sets `lang` only if not set yet
   (a `-x` or `++` compiler wins).
9. `target` = collected `--target`.
10. **Implicit compiler info** (§3) is added when `lang` is set and either
    (no `--gcc-toolchain=` among the kept options **and** not "same clang"),
    or a `--compiler-info-file` exists.
11. Unless `--keep-gcc-include-fixed`: drop implicit include dirs whose last
    path component is `include-fixed`.
12. Unless `--keep-gcc-intrin`: drop implicit include dirs that contain a
    file matching `*intrin.h`; also drop include-like options
    (`-iquote -I -F -isystem -iprefix -iwithprefix -iwithprefixbefore`, joined
    or separate) from `analyzer_options` whose value is an existing directory
    containing `*intrin.h`.

### 2.1 GCC ignore list

Prefix regexes (joined with `|`, matched at the start of the argument):

```
-fallow-fetchr-insn -fcall-saved- -fcond-mismatch -fconserve-stack -fcrossjumping
-fcse-follow-jumps -fcse-skip-blocks -fcx-limited-range$ -fdump-rtl.* -fext-.*-literals
-ffixed-r2 -ffp$ -mfp16-format -mmitigate-rop -fgcse-lm -fhoist-adjacent-loads
-findirect-inlining -finline-limit -finline-local-initialisers -fipa-sra
-fmacro-prefix-map -fmerge-constants -fno-aggressive-loop-optimizations
-f(no-)?allow-store-data-races -fno-canonical-system-headers -f(no-)?code-hoisting
-fno-delete-null-pointer-checks -fno-defer-pop -fno-extended-identifiers
-fno-freestanding -fno-jump-table
-fno-keep-inline-dllexport-fno-keep-static-consts      (two entries concatenated by a missing comma in the source)
-fno-lifetime-dse -f(no-)?printf-return-value -f(no-)?reorder-functions
-fno-strength-reduce -fno-toplevel-reorder -fno-unit-at-a-time
-fno-var-tracking-assignments -fno-tree-dominator-opts -fobjc-link-runtime
-fpartial-inlining -fpeephole2 -fr$ -fregmove -frename-registers
-frerun-cse-after-loop -fs$ -fsanitize=bounds-strict -fsched-pressure -fsched-spec
-fstack-usage -fstack-reuse -fthread-jumps -ftree-pre -ftree-switch-conversion
-ftree-tail-merge -m(no-)?abm -m(no-)?sdata -m(no-)?spe -m(no-)?string$ -m(no-)?dsbt
-m(no-)?fixed-ssp -m(no-)?pointers-to-nested-functions -m(no-)?word-relocations
-mno-fp-ret-in-387 -mpreferred-stack-boundary -mpcrel-func-addr -mrecord-mcount$
-maccumulate-outgoing-args -mcall-aixdesc -mppa3-addr-bug -mtraceback= -mtext=
-misa= -mfunction-return= -mindirect-branch-register -mindirect-branch=
-mfix-cortex-m3-ldrd$ -mmultiple$ -msahf$ -mskip-rax-setup$ -mthumb-interwork$
-mupdate$ -mapcs -fno-merge-const-bfstores$ -fno-ipa-sra$ -mno-thumb-interwork$
-mno-sched-prolog -DNDEBUG$ -save-temps -Werror -Wno-error -pedantic-errors
-fprofile -w -g(.+)?$ -flto -mxl -mfloat-gprs -mabi
```

Options skipped **with parameters** (count): `-install_name` 1,
`-exported_symbols_list` 1, `-current_version` 1, `-compatibility_version` 1,
`-init$` 1, `-e$` 1, `-seg1addr` 1, `-bundle_loader` 1, `-multiply_defined` 1,
`-sectorder` 3, `--param$` 1, `-u$` 1, `--serialize-diagnostics` 1,
`-framework` 1, `-filelist` 1.

(Infer uses the same GCC ignore regex on the original command.)

### 2.2 Include-like options

Matched at the start of the argument by
`(--sysroot|-sdkroot|--include|-include|-iquote|-[DIUF]|-idirafter|-isystem|-imacros|-isysroot|-iprefix|-iwithprefix|-iwithprefixbefore)`.
- Joined (`-Ifoo`) or separate (`-I foo`, consumes the next argument).
- For `-I -idirafter -iquote -isysroot -isystem -sysroot --sysroot -include
  --include -imacros -iprefix -iwithprefix -iwithprefixbefore`: if the
  parameter does not start with `=` (or the flag is a sysroot flag), it is
  normalized: a leading `=` is removed (and the output becomes separate
  form), then `normpath(join(directory, param))` (so relative paths become
  absolute; absolute paths stay).
- `-D`, `-U`, `-F` parameters are kept as they are.
- Quirk: the regex alternation tries `-iwithprefix` before
  `-iwithprefixbefore`, so `-iwithprefixbefore X` is treated as the joined
  flag `-iwithprefix` with parameter `before`, which is then turned into the
  path `<directory>/before`; `X` is processed as a separate argument.
- Output keeps the joined/separate form of the input (except the `=`
  case).

## 3. Implicit compiler information

Key: `(compiler, lang, sorted extra flags)`, where extra flags are the kept
options matching `-m(32|64)|-{1,2}std=|-{1,2}stdlib=|-nostdinc|--driver-mode=`
plus the first `--sysroot` option (normalized to `--sysroot=<path>`).

For an unknown key, run (with `LC_ALL=C`, stdin empty, stderr captured):

| Value | Command | Extraction |
|---|---|---|
| `compiler_includes` | `<compiler> <extra flags> -E -x <lang> - -v` | lines after `#include <...> search starts here:` up to `End of search list.`, stripped; ` (framework directory)` suffix removed; each `normpath`ed |
| `target` | `<compiler> -v` | second token of the last line whose first token is `Target:` |
| `compiler_standard` | compile a temporary `.c`/`.cpp` file with `#error CC_FOUND_STANDARD_VER#<n>` chosen by `__STDC_VERSION__` (≥201710→17, ≥201112→11, ≥199901→99, ≥199409→94, else 90) or `__cplusplus` (≥201703→17, ≥201402→14, ≥201103→11, else 98) | `-std=iso9899:199409` for 94; else `-std=gnu<n>` (C) / `-std=gnu++<n>` (C++); empty if not found |

Values are set on the action only for fields that are still empty.

The cache is shared by the parallel parser workers. Failing commands give
empty values.

### Compiler info file

Written to `<out>/compiler_info.json` after parsing (or to `-o` with
`--dump-compiler-info-file`). Format: one JSON object; keys are the JSON
text of `[compiler, lang, [extra flags]]` (Python `json.dumps` spacing:
`"[\"/usr/bin/gcc\", \"c\", []]"`), values are
`{"compiler_includes": [...], "compiler_standard": "...", "target": "..."}`.

`--compiler-info-file F`: if `F` exists, it is loaded (keys parsed back) and
**replaces** the cache for every entry; the commands above are not run. An
entry whose key is not in the file gets no implicit info.

## 4. Filtering and uniqueing

Entries are parsed in parallel (`-j` processes); results are processed in
input order:

1. Parse error → skipped count + 1.
2. Skipped by the analysis skip handlers, and (no CTU/statistics, **or** also
   skipped by the pre-analysis skip handlers) → skipped + 1. (With CTU or
   statistics, skipped files are still parsed so their data can be
   collected.)
3. No `lang` → skipped + 1.
4. Action type not COMPILE → skipped + 1 (link, preprocess, info).
5. Uniqueing by `--compile-uniqueing`:

   | Mode | Key | On duplicate |
   |---|---|---|
   | `none` | the action (hash of `analyzer_options + analyzer_type + target + source`, equality by `original_command`) | identical commands are kept once |
   | `strict` | source | error `Build Action uniqueing failed as both '<cmd1>' and '<cmd2>'`, exit 1 |
   | `alpha` | source | keep the action with the lexicographically smaller `output` (`-o` value) |
   | `symlink` | canonical source path | keep the first |
   | regex | source | keep the first; replace it by a later one whose `original_command` matches the regex (Python `re.match`, i.e. anchored at the start) while the kept one does not; if both match → error `Build Action uniqueing failed as both \n <c1>\n and \n <c2> \n match regex pattern:<re>`, exit 1 |

Output: the kept actions (dictionary insertion order) and the skipped count.

An exception of type ValueError/KeyError/TypeError during this phase →
`The compile database is empty.` (empty input) or
`The compile database is not valid.`, exit 1.
